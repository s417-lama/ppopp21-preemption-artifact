/* -*- c++ -*- ----------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#ifdef INTEGRATE_CLASS

IntegrateStyle(verlet/kk,VerletKokkos)
IntegrateStyle(verlet/kk/device,VerletKokkos)
IntegrateStyle(verlet/kk/host,VerletKokkos)

#else

#ifndef LMP_VERLET_KOKKOS_H
#define LMP_VERLET_KOKKOS_H

#include "verlet.h"
#include "kokkos_type.h"
#include "atom_kokkos.h"
#include "neigh_list_kokkos.h"
#include "memory_kokkos.h"
#include <mpi.h>
#include <sys/resource.h>
#include <sys/syscall.h>

#define HANDLE_ERROR(ret, msg)                        \
    if (ret != ABT_SUCCESS) {                         \
        fprintf(stderr, "ERROR[%d]: %s\n", ret, msg); \
        exit(EXIT_FAILURE);                           \
    }

namespace LAMMPS_NS {

typedef struct { long val; } __attribute((aligned(64))) counter_t;

struct task {
  virtual void * execute() = 0;
};

template<typename Func>
struct callable_task : task {
  Func func;
  callable_task(Func func_) : func(func_) {}
  void* execute() { func(); return NULL; }
};

#ifdef KOKKOS_ENABLE_OPENMP
static int pthreads_nice;

static void *invoke(void *arg_)
{
  if (pthreads_nice > 0) {
    pid_t tid = syscall(SYS_gettid);
    int ret = setpriority(PRIO_PROCESS, tid, pthreads_nice);
    if (ret != 0) {
      printf("error: setpriority\n");
      abort();
    }
  }

  task *arg = (task *)arg_;
  arg->execute();
  return NULL;
}
#else
static void invoke(void *arg_)
{
  task *arg = (task *)arg_;
  arg->execute();
}
#endif

class VerletKokkos : public Verlet {
 public:
  VerletKokkos(class LAMMPS *, int, char **);
  ~VerletKokkos() {}
  void setup(int);
  void setup_minimal(int);
  void run(int);

  KOKKOS_INLINE_FUNCTION
  void operator() (const int& i) const {
    f(i,0) += f_merge_copy(i,0);
    f(i,1) += f_merge_copy(i,1);
    f(i,2) += f_merge_copy(i,2);
  }


 protected:
  DAT::t_f_array f_merge_copy,f;

  void force_clear();

  int enable_analysis;
  int async_analysis;
  int n_analysis_threads;
  task** ts;
  counter_t* bond_counts;
  int analysis_started;
  int analysis_intvl;
#ifdef KOKKOS_ENABLE_OPENMP
  pthread_t* threads;
#endif
#ifdef KOKKOS_ENABLE_ARGOBOTS
  ABT_thread* threads;
#endif

  template<class DeviceType>
  void analysis(NeighListKokkos<DeviceType>* list)
  {
    int ret;
    const double BOND_R1 = 0.95;
    const double BOND_R2 = 1.3;

    int nlocal = atomKK->nlocal;
    typename ArrayTypes<DeviceType>::t_x_array x = atomKK->k_x.view<DeviceType>();
    typename ArrayTypes<DeviceType>::t_x_array x_;
    memoryKK->create_kokkos(x_, nlocal + atomKK->nghost, 3, "analysis");

    /* copy */
    Kokkos::parallel_for(Kokkos::RangePolicy<DeviceType>(0, nlocal + atomKK->nghost), [&](int i) {
      x_(i,0) = x(i,0);
      x_(i,1) = x(i,1);
      x_(i,2) = x(i,2);
    });

#ifdef KOKKOS_ENABLE_OPENMP
    // unset affinity
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    for (int i = 0; i < Kokkos::OpenMP::thread_pool_size(); i++) {
      CPU_SET(i, &cpus);
    }
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
#endif

    int nb = (nlocal + n_analysis_threads - 1) / n_analysis_threads;

    for (int tid = 0; tid < n_analysis_threads; tid++) {
      auto thread_fn = [=]() {
        int ifrom = tid * nb;
        int ito = ((tid + 1) * nb > nlocal) ? nlocal : (tid + 1) * nb;

        bond_counts[tid].val = 0;
        for (int i = ifrom; i < ito; i++) {
          const AtomNeighborsConst neighbors_i = list->get_neighbors_const(i);
          const int jnum = list->d_numneigh[i];
          for (int jj = 0; jj < jnum; jj++) {
            int j = neighbors_i(jj);
            X_FLOAT dx = x_(i,0) - x_(j,0);
            X_FLOAT dy = x_(i,1) - x_(j,1);
            X_FLOAT dz = x_(i,2) - x_(j,2);
            X_FLOAT dist = sqrt(dx*dx + dy*dy + dz*dz);
            if ((dist >= BOND_R1) && (dist <= BOND_R2)) {
              bond_counts[tid].val++;
            }
          }
        }
      };

      ts[tid] = new callable_task<decltype(thread_fn)>(thread_fn);
#ifdef KOKKOS_ENABLE_OPENMP
      pthread_create(&threads[tid], &attr, invoke, ts[tid]);
#endif
#ifdef KOKKOS_ENABLE_ARGOBOTS
      ABT_thread_attr attr;
      ABT_thread_attr_create(&attr);
      ABT_thread_attr_set_preemption_type(attr, ABT_PREEMPTION_YIELD);
      ret = ABT_thread_create(Kokkos::Impl::g_analysis_pools[tid % (Kokkos::Impl::g_num_xstreams - 1) + 1],
                              invoke,
                              ts[tid],
                              attr,
                              &threads[tid]);
      HANDLE_ERROR(ret, "ABT_thread_create");
#endif
    }

    memoryKK->destroy_kokkos(x_);

    analysis_started = 1;
  }

  void analysis_wait()
  {
    int ret;
    if (analysis_started) {
      long bond_count = 0;
      for (int tid = 0; tid < n_analysis_threads; tid++) {
#ifdef KOKKOS_ENABLE_OPENMP
        pthread_join(threads[tid], NULL);
#endif
#ifdef KOKKOS_ENABLE_ARGOBOTS
        ret = ABT_thread_free(&threads[tid]);
        HANDLE_ERROR(ret, "ABT_thread_free");
#endif
        delete ts[tid];
        bond_count += bond_counts[tid].val;
      }

      int rank;
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
      char filename[20];
      sprintf(filename, "bond_%d.ignore", rank);
      FILE *fp = fopen(filename, "w+");
      fprintf(fp, "bond: %ld\n", bond_count);
      fclose(fp);

      analysis_started = 0;
    }
  }
};

}

#endif
#endif

/* ERROR/WARNING messages:

*/
