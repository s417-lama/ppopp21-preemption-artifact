/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 2.0
//              Copyright (2014) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#include <Kokkos_Macros.hpp>
#if defined( KOKKOS_ENABLE_ARGOBOTS )

#include <abt.h>
#include <mpi.h>

#include <cstdio>
#include <cstdlib>

#include <limits>
#include <iostream>
#include <vector>

#include <Kokkos_Core.hpp>

#include <impl/Kokkos_Error.hpp>
#include <impl/Kokkos_CPUDiscovery.hpp>
#include <impl/Kokkos_Profiling_Interface.hpp>

#include "logger.h"

/* Scheduler */

namespace Kokkos {
namespace Impl {

ABT_pool* g_pools;
ABT_pool* g_analysis_pools;
int g_num_xstreams;
int g_num_threads;
int g_num_pgroups;
ABT_xstream* g_xstreams;
ABT_sched* g_scheds;
ABT_preemption_group* g_preemption_groups;
int g_enable_preemption;
busytime_measure_t* g_busytimes;
int g_measure_busytime;

static int sched_init(ABT_sched sched, ABT_sched_config config)
{
  char *s;
  s = getenv("LAMMPS_MEASURE_BUSYTIME");
  if (s) {
    g_measure_busytime = atoi(s);
  } else {
    g_measure_busytime = 0;
  }
  return ABT_SUCCESS;
}

static void run_unit(ABT_unit unit, ABT_pool pool, int rank)
{
  if (g_measure_busytime) {
    volatile busytime_measure_t* volatile p_busytime = &g_busytimes[rank];

    p_busytime->executing = 1;

    p_busytime->last_time = mlog_clock_gettime_in_nsec();
    ABT_xstream_run_unit(unit, pool);
    uint64_t t2 = mlog_clock_gettime_in_nsec();

    if (p_busytime->begin_time > 0) {
      uint64_t t1 = std::max(p_busytime->begin_time, p_busytime->last_time);
      p_busytime->acc += t2 - t1;
    }

    p_busytime->executing = 0;
  } else {
    ABT_xstream_run_unit(unit, pool);
  }
}

static void sched_run(ABT_sched sched)
{
  uint32_t work_count = 0;
  int num_pools;
  ABT_pool *pools;
  ABT_unit unit;
  ABT_bool stop;
  int event_freq = 100;
  unsigned seed = time(NULL);

  ABT_sched_get_num_pools(sched, &num_pools);
  pools = (ABT_pool *)malloc(num_pools * sizeof(ABT_pool));
  ABT_sched_get_pools(sched, num_pools, 0, pools);

  int rank;
  ABT_xstream_self_rank(&rank);

  while (1) {
    ABT_pool_pop(pools[0], &unit);
    if (unit != ABT_UNIT_NULL) {
      run_unit(unit, pools[0], rank);
    } else {
      /* Pop an analysis task */
      ABT_pool_pop(pools[1], &unit);
      if (unit != ABT_UNIT_NULL) {
        void *bp = logger_begin_tl(rank);

        run_unit(unit, pools[1], rank);

        logger_end_tl(rank, bp, "analysis");
      } else if (num_pools > 2) {
        /* Steal an analysis task from other pools */
        int target = (num_pools == 3) ? 1 : (rand_r(&seed) % (num_pools-2) + 1);
        ABT_pool_pop(pools[target], &unit);
        if (unit != ABT_UNIT_NULL) {
          void *bp = logger_begin_tl(rank);

          run_unit(unit, pools[target], rank);

          logger_end_tl(rank, bp, "analysis");
        }
      }
    }

    if (++work_count >= event_freq) {
      work_count = 0;
      ABT_sched_has_to_stop(sched, &stop);
      if (stop == ABT_TRUE) break;
      ABT_xstream_check_events(sched);
    }
  }

  free(pools);
}

static int sched_free(ABT_sched sched)
{
  return ABT_SUCCESS;
}

static ABT_sched_def sched_def = {
  .type = ABT_SCHED_TYPE_ULT,
  .init = sched_init,
  .run = sched_run,
  .free = sched_free,
  .get_migr_pool = NULL
};

} // namespace Impl
} // namespace Kokkos

/* Pool */

namespace Kokkos {
namespace Impl {

struct lifo_unit {
  struct lifo_unit *p_next;
  ABT_pool pool;
  union {
    ABT_thread thread;
    ABT_task   task;
  };
  ABT_unit_type type;
};

struct lifo_pool_data {
  ABT_mutex mutex;
  size_t num_units;
  struct lifo_unit *p_head;
};

typedef struct lifo_unit unit_t;
typedef struct lifo_pool_data data_t;

static inline data_t *pool_get_data_ptr(void *p_data)
{
  return (data_t *)p_data;
}

int pool_init(ABT_pool pool, ABT_pool_config config)
{
  int abt_errno = ABT_SUCCESS;

  data_t *p_data = (data_t *)malloc(sizeof(data_t));
  if (!p_data) return ABT_ERR_MEM;

  /* Initialize the mutex */
  ABT_mutex_create(&p_data->mutex);

  p_data->num_units = 0;
  p_data->p_head = NULL;

  ABT_pool_set_data(pool, p_data);

  return abt_errno;
}

static int pool_free(ABT_pool pool)
{
  int abt_errno = ABT_SUCCESS;
  void *data;
  ABT_pool_get_data(pool, &data);
  data_t *p_data = pool_get_data_ptr(data);

  free(p_data);

  return abt_errno;
}

static size_t pool_get_size(ABT_pool pool)
{
  void *data;
  ABT_pool_get_data(pool, &data);
  data_t *p_data = pool_get_data_ptr(data);
  return p_data->num_units;
}

static void pool_push(ABT_pool pool, ABT_unit unit)
{
  void *data;
  ABT_pool_get_data(pool, &data);
  data_t *p_data = pool_get_data_ptr(data);
  unit_t *p_unit = (unit_t *)unit;

  ABT_mutex_spinlock(p_data->mutex);
  if (p_data->num_units == 0) {
    p_unit->p_next = p_unit;
    p_data->p_head = p_unit;
  } else {
    unit_t *p_head = p_data->p_head;
    p_unit->p_next = p_head;
    p_data->p_head = p_unit;
  }
  p_data->num_units++;

  p_unit->pool = pool;
  ABT_mutex_unlock(p_data->mutex);
}

static ABT_unit pool_pop(ABT_pool pool)
{
  void *data;
  ABT_pool_get_data(pool, &data);
  data_t *p_data = pool_get_data_ptr(data);
  unit_t *p_unit = NULL;
  ABT_unit h_unit = ABT_UNIT_NULL;

  if (p_data->num_units > 0) {
    ABT_mutex_spinlock(p_data->mutex);
    if (__atomic_load_n(&p_data->num_units, __ATOMIC_ACQUIRE) > 0) {
      p_unit = p_data->p_head;
      if (p_data->num_units == 1) {
        p_data->p_head = NULL;
      } else {
        p_data->p_head = p_unit->p_next;
      }
      p_data->num_units--;

      p_unit->p_next = NULL;
      p_unit->pool = ABT_POOL_NULL;

      h_unit = (ABT_unit)p_unit;
    }
    ABT_mutex_unlock(p_data->mutex);
  }

  return h_unit;
}

/* Unit functions */

static ABT_unit_type unit_get_type(ABT_unit unit)
{
  unit_t *p_unit = (unit_t *)unit;
  return p_unit->type;
}

static ABT_thread unit_get_thread(ABT_unit unit)
{
  ABT_thread h_thread;
  unit_t *p_unit = (unit_t *)unit;
  if (p_unit->type == ABT_UNIT_TYPE_THREAD) {
    h_thread = p_unit->thread;
  } else {
    h_thread = ABT_THREAD_NULL;
  }
  return h_thread;
}

static ABT_task unit_get_task(ABT_unit unit)
{
  ABT_task h_task;
  unit_t *p_unit = (unit_t *)unit;
  if (p_unit->type == ABT_UNIT_TYPE_TASK) {
    h_task = p_unit->task;
  } else {
    h_task = ABT_TASK_NULL;
  }
  return h_task;
}

static ABT_bool unit_is_in_pool(ABT_unit unit)
{
  unit_t *p_unit = (unit_t *)unit;
  return (p_unit->pool != ABT_POOL_NULL) ? ABT_TRUE : ABT_FALSE;
}

static ABT_unit unit_create_from_thread(ABT_thread thread)
{
  unit_t *p_unit = (unit_t*)malloc(sizeof(unit_t));
  if (!p_unit) return ABT_UNIT_NULL;

  p_unit->p_next = NULL;
  p_unit->pool   = ABT_POOL_NULL;
  p_unit->thread = thread;
  p_unit->type   = ABT_UNIT_TYPE_THREAD;

  return (ABT_unit)p_unit;
}

static ABT_unit unit_create_from_task(ABT_task task)
{
  unit_t *p_unit = (unit_t*)malloc(sizeof(unit_t));
  if (!p_unit) return ABT_UNIT_NULL;

  p_unit->p_next = NULL;
  p_unit->pool   = ABT_POOL_NULL;
  p_unit->task   = task;
  p_unit->type   = ABT_UNIT_TYPE_TASK;

  return (ABT_unit)p_unit;
}

static void unit_free(ABT_unit *unit)
{
  free(*unit);
  *unit = ABT_UNIT_NULL;
}
    ABT_pool_access access; /* Access type */

    /* Functions to manage units */
    ABT_unit_get_type_fn           u_get_type;
    ABT_unit_get_thread_fn         u_get_thread;
    ABT_unit_get_task_fn           u_get_task;
    ABT_unit_is_in_pool_fn         u_is_in_pool;
    ABT_unit_create_from_thread_fn u_create_from_thread;
    ABT_unit_create_from_task_fn   u_create_from_task;
    ABT_unit_free_fn               u_free;

    /* Functions to manage the pool */
    ABT_pool_init_fn          p_init;
    ABT_pool_get_size_fn      p_get_size;
    ABT_pool_push_fn          p_push;
    ABT_pool_pop_fn           p_pop;
    ABT_pool_pop_timedwait_fn p_pop_timedwait;
    ABT_pool_remove_fn        p_remove;
    ABT_pool_free_fn          p_free;
    ABT_pool_print_all_fn     p_print_all;

static ABT_pool_def pool_def = {
  .access               = ABT_POOL_ACCESS_MPMC,
  .u_get_type           = unit_get_type,
  .u_get_thread         = unit_get_thread,
  .u_get_task           = unit_get_task,
  .u_is_in_pool         = unit_is_in_pool,
  .u_create_from_thread = unit_create_from_thread,
  .u_create_from_task   = unit_create_from_task,
  .u_free               = unit_free,
  .p_init               = pool_init,
  .p_get_size           = pool_get_size,
  .p_push               = pool_push,
  .p_pop                = pool_pop,
  .p_pop_timedwait      = NULL,
  .p_remove             = NULL,
  .p_free               = pool_free,
  .p_print_all          = NULL
};

} // namespace Impl
} // namespace Kokkos


//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {

//----------------------------------------------------------------------------

#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
void Argobots::initialize()
#else
void Argobots::impl_initialize()
#endif
{
  char *s;
  s = getenv("LAMMPS_ABT_NUM_XSTREAMS");
  if (s) {
    Impl::g_num_xstreams = atoi(s);
  } else {
    Impl::g_num_xstreams = 1;
  }

  s = getenv("LAMMPS_ABT_NUM_THREADS");
  if (s) {
    Impl::g_num_threads = atoi(s);
  } else {
    Impl::g_num_threads = 1;
  }

  s = getenv("LAMMPS_ABT_ENABLE_PREEMPTION");
  if (s) {
    Impl::g_enable_preemption = atoi(s);
  } else {
    Impl::g_enable_preemption = 0;
  }

  logger_init(Impl::g_num_xstreams);

  /* setup for busy time measurement */
  posix_memalign((void**)&Impl::g_busytimes, 64, Impl::g_num_xstreams * sizeof(busytime_measure_t));
  for (int i = 0; i < Impl::g_num_xstreams; i++) {
    Impl::g_busytimes[i].acc = 0;
    Impl::g_busytimes[i].begin_time = 0;
    Impl::g_busytimes[i].last_time = 0;
    Impl::g_busytimes[i].executing = 0;
  }

  int ret;
  ABT_init(0, NULL);

  /* Create pools */
  Impl::g_pools = (ABT_pool *)malloc(sizeof(ABT_pool) * Impl::g_num_xstreams);
  Impl::g_analysis_pools = (ABT_pool *)malloc(sizeof(ABT_pool) * Impl::g_num_xstreams);
  for (int i = 0; i < Impl::g_num_xstreams; i++) {
    ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &Impl::g_pools[i]);
    /* ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &Impl::g_analysis_pools[i]); */
    ABT_pool_create(&Impl::pool_def, ABT_POOL_CONFIG_NULL, &Impl::g_analysis_pools[i]);
  }

  /* Create scheds */
  Impl::g_scheds = (ABT_sched *)malloc(sizeof(ABT_sched) * Impl::g_num_xstreams);
  for (int i = 0; i < Impl::g_num_xstreams; i++) {
    ABT_pool mypools[Impl::g_num_xstreams + 1];
    mypools[0] = Impl::g_pools[i];
    for (int k = 0; k < Impl::g_num_xstreams; k++) {
        mypools[k + 1] = Impl::g_analysis_pools[(i + k) % Impl::g_num_xstreams];
    }
    ABT_sched_create(&Impl::sched_def, Impl::g_num_xstreams + 1, mypools, ABT_SCHED_CONFIG_NULL, &Impl::g_scheds[i]);
  }

  /* Primary ES creation */
  Impl::g_xstreams = (ABT_xstream *)malloc(sizeof(ABT_xstream) * Impl::g_num_xstreams);
  ABT_xstream_self(&Impl::g_xstreams[0]);
  ABT_xstream_set_main_sched(Impl::g_xstreams[0], Impl::g_scheds[0]);

  /* Secondary ES creation */
  for (int i = 1; i < Impl::g_num_xstreams; i++) {
    ret = ABT_xstream_create(Impl::g_scheds[i], &Impl::g_xstreams[i]);
    HANDLE_ERROR(ret, "ABT_xstream_create");
  }

  if (Impl::g_enable_preemption) {
    Impl::g_num_pgroups = 1;
    Impl::g_preemption_groups = (ABT_preemption_group *)malloc(sizeof(ABT_preemption_group) * Impl::g_num_pgroups);
    ABT_preemption_timer_create_groups(Impl::g_num_pgroups, Impl::g_preemption_groups);
    for (int i = 0; i < Impl::g_num_pgroups; i++) {
      int begin = i * Impl::g_num_xstreams / Impl::g_num_pgroups;
      int end   = (i + 1) * Impl::g_num_xstreams / Impl::g_num_pgroups;
      if (end > Impl::g_num_xstreams) end = Impl::g_num_xstreams;
      ABT_preemption_timer_set_xstreams(Impl::g_preemption_groups[i], end - begin, &Impl::g_xstreams[begin]);
    }
    for (int i = 0; i < Impl::g_num_pgroups; i++) {
      ABT_preemption_timer_start(Impl::g_preemption_groups[i]);
    }
  }
}

//----------------------------------------------------------------------------

#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
void Argobots::finalize()
#else
void Argobots::impl_finalize()
#endif
{
  int ret;

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (Impl::g_enable_preemption) {
    for (int i = 0; i < Impl::g_num_pgroups; i++) {
      ABT_preemption_timer_delete(Impl::g_preemption_groups[i]);
    }
    free(Impl::g_preemption_groups);
  }

  /* Join and free Execution Streams */
  for (int i = 1; i < Impl::g_num_xstreams; i++) {
    ret = ABT_xstream_free(&Impl::g_xstreams[i]);
    HANDLE_ERROR(ret, "ABT_xstream_free");
  }

  /* Free scheds */
  for (int i = 1; i < Impl::g_num_xstreams; i++) {
    ABT_sched_free(&Impl::g_scheds[i]);
  }

  ret = ABT_finalize();
  HANDLE_ERROR(ret, "ABT_finalize");

  free(Impl::g_scheds);
  free(Impl::g_xstreams);
  free(Impl::g_pools);

  char filename[20];
  sprintf(filename, "mlog_%d.ignore", rank);
  FILE *fp = fopen(filename, "w+");
  logger_flush(fp);
  fclose(fp);
}

//----------------------------------------------------------------------------

void Argobots::print_configuration( std::ostream & s , const bool verbose )
{
}

}

#else
void KOKKOS_CORE_SRC_ARGOBOTS_EXEC_PREVENT_LINK_ERROR() {}
#endif //KOKKOS_ENABLE_ARGOBOTS
