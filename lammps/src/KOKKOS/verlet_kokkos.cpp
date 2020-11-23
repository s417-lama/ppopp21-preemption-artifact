/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "verlet_kokkos.h"
#include "neighbor.h"
#include "domain.h"
#include "comm.h"
#include "atom_kokkos.h"
#include "atom_masks.h"
#include "force.h"
#include "pair.h"
#include "bond.h"
#include "angle.h"
#include "dihedral.h"
#include "improper.h"
#include "kspace.h"
#include "output.h"
#include "update.h"
#include "modify.h"
#include "timer.h"
#include "memory_kokkos.h"
#include "kokkos.h"

using namespace LAMMPS_NS;

template<class ViewA, class ViewB>
struct ForceAdder {
  ViewA a;
  ViewB b;
  ForceAdder(const ViewA& a_, const ViewB& b_):a(a_),b(b_) {}
  KOKKOS_INLINE_FUNCTION
  void operator() (const int& i) const {
    a(i,0) += b(i,0);
    a(i,1) += b(i,1);
    a(i,2) += b(i,2);
  }
};

/* ---------------------------------------------------------------------- */

template<class View>
struct Zero {
  View v;
  Zero(const View &v_):v(v_) {}
  KOKKOS_INLINE_FUNCTION
  void operator()(const int &i) const {
    v(i,0) = 0;
    v(i,1) = 0;
    v(i,2) = 0;
  }
};

/* ---------------------------------------------------------------------- */

VerletKokkos::VerletKokkos(LAMMPS *lmp, int narg, char **arg) :
  Verlet(lmp, narg, arg)
{
  atomKK = (AtomKokkos *) atom;
}

/* ----------------------------------------------------------------------
   setup before run
------------------------------------------------------------------------- */

void VerletKokkos::setup(int flag)
{
  if (comm->me == 0 && screen) {
    fprintf(screen,"Setting up Verlet run ...\n");
    if (flag) {
      fprintf(screen,"  Unit style    : %s\n", update->unit_style);
      fprintf(screen,"  Current step  : " BIGINT_FORMAT "\n",
              update->ntimestep);
      fprintf(screen,"  Time step     : %g\n", update->dt);
      timer->print_timeout(screen);
    }
  }

  update->setupflag = 1;

  // setup domain, communication and neighboring
  // acquire ghosts
  // build neighbor lists

  atomKK->sync(Host,ALL_MASK);
  atomKK->modified(Host,ALL_MASK);

  atomKK->setup();
  modify->setup_pre_exchange();
      // debug
  atomKK->sync(Host,ALL_MASK);
  atomKK->modified(Host,ALL_MASK);
  if (triclinic) domain->x2lamda(atomKK->nlocal);
  domain->pbc();

  atomKK->sync(Host,ALL_MASK);


  domain->reset_box();
  comm->setup();
  if (neighbor->style) neighbor->setup_bins();

  comm->exchange();

  if (atomKK->sortfreq > 0) atomKK->sort();

  comm->borders();

  if (triclinic) domain->lamda2x(atomKK->nlocal+atomKK->nghost);

  atomKK->sync(Host,ALL_MASK);

  domain->image_check();
  domain->box_too_small_check();
  modify->setup_pre_neighbor();

  atomKK->modified(Host,ALL_MASK);

  neighbor->build(1);
  modify->setup_post_neighbor();
  neighbor->ncalls = 0;

  // compute all forces

  ev_set(update->ntimestep);
  force_clear();
  modify->setup_pre_force(vflag);

  if (pair_compute_flag) {
    atomKK->sync(force->pair->execution_space,force->pair->datamask_read);
    force->pair->compute(eflag,vflag);
    atomKK->modified(force->pair->execution_space,force->pair->datamask_modify);
    timer->stamp(Timer::PAIR);
  }
  else if (force->pair) force->pair->compute_dummy(eflag,vflag);


  if (atomKK->molecular) {
    if (force->bond) {
      atomKK->sync(force->bond->execution_space,force->bond->datamask_read);
      force->bond->compute(eflag,vflag);
      atomKK->modified(force->bond->execution_space,force->bond->datamask_modify);
    }
    if (force->angle) {
      atomKK->sync(force->angle->execution_space,force->angle->datamask_read);
      force->angle->compute(eflag,vflag);
      atomKK->modified(force->angle->execution_space,force->angle->datamask_modify);
    }
    if (force->dihedral) {
      atomKK->sync(force->dihedral->execution_space,force->dihedral->datamask_read);
      force->dihedral->compute(eflag,vflag);
      atomKK->modified(force->dihedral->execution_space,force->dihedral->datamask_modify);
    }
    if (force->improper) {
      atomKK->sync(force->improper->execution_space,force->improper->datamask_read);
      force->improper->compute(eflag,vflag);
      atomKK->modified(force->improper->execution_space,force->improper->datamask_modify);
    }
    timer->stamp(Timer::BOND);
  }

  if(force->kspace) {
    force->kspace->setup();
    if (kspace_compute_flag) {
      atomKK->sync(force->kspace->execution_space,force->kspace->datamask_read);
      force->kspace->compute(eflag,vflag);
      atomKK->modified(force->kspace->execution_space,force->kspace->datamask_modify);
      timer->stamp(Timer::KSPACE);
    } else force->kspace->compute_dummy(eflag,vflag);
  }
  if (force->newton) comm->reverse_comm();

  lmp->kokkos->auto_sync = 0;
  modify->setup(vflag);
  output->setup(flag);
  lmp->kokkos->auto_sync = 1;
  update->setupflag = 0;

  char *s;
  s = getenv("LAMMPS_ENABLE_ANALYSIS");
  if (s) {
    enable_analysis = atoi(s);
  } else {
    enable_analysis = 0;
  }

  s = getenv("LAMMPS_ASYNC_ANALYSIS");
  if (s) {
    async_analysis = atoi(s);
  } else {
    async_analysis = 0;
  }

  s = getenv("LAMMPS_NUM_ANALYSIS_THREADS");
  if (s) {
    n_analysis_threads = atoi(s);
  } else {
    n_analysis_threads = 1;
  }

  s = getenv("LAMMPS_ANALYSIS_INTERVAL");
  if (s) {
    analysis_intvl = atoi(s);
  } else {
    analysis_intvl = 1;
  }

#ifdef KOKKOS_ENABLE_OPENMP
  s = getenv("LAMMPS_ANALYSIS_PTHREAD_NICE");
  if (s) {
    pthreads_nice = atoi(s);
  } else {
    pthreads_nice = 0;
  }
#endif

  analysis_started = 0;

#ifdef KOKKOS_ENABLE_OPENMP
  threads = (pthread_t*)malloc(sizeof(pthread_t) * n_analysis_threads);
#endif
#ifdef KOKKOS_ENABLE_ARGOBOTS
  threads = (ABT_thread*)malloc(sizeof(ABT_thread) * n_analysis_threads);
#endif
  ts = (task**)malloc(sizeof(task*) * n_analysis_threads);
  posix_memalign((void**)&bond_counts, 64, sizeof(counter_t) * n_analysis_threads);
}

/* ----------------------------------------------------------------------
   setup without output
   flag = 0 = just force calculation
   flag = 1 = reneighbor and force calculation
------------------------------------------------------------------------- */

void VerletKokkos::setup_minimal(int flag)
{
  update->setupflag = 1;

  // setup domain, communication and neighboring
  // acquire ghosts
  // build neighbor lists

  if (flag) {
    atomKK->sync(Host,ALL_MASK);
    atomKK->modified(Host,ALL_MASK);

    modify->setup_pre_exchange();
      // debug
      atomKK->sync(Host,ALL_MASK);
      atomKK->modified(Host,ALL_MASK);

    if (triclinic) domain->x2lamda(atomKK->nlocal);
    domain->pbc();

    atomKK->sync(Host,ALL_MASK);

    domain->reset_box();
    comm->setup();
    if (neighbor->style) neighbor->setup_bins();
    comm->exchange();
    comm->borders();
    if (triclinic) domain->lamda2x(atomKK->nlocal+atomKK->nghost);

    atomKK->sync(Host,ALL_MASK);

    domain->image_check();
    domain->box_too_small_check();
    modify->setup_pre_neighbor();

    atomKK->modified(Host,ALL_MASK);

    neighbor->build(1);
    modify->setup_post_neighbor();
    neighbor->ncalls = 0;
  }

  // compute all forces

  ev_set(update->ntimestep);
  force_clear();
  modify->setup_pre_force(vflag);

  if (pair_compute_flag) {
    atomKK->sync(force->pair->execution_space,force->pair->datamask_read);
    force->pair->compute(eflag,vflag);
    atomKK->modified(force->pair->execution_space,force->pair->datamask_modify);
    timer->stamp(Timer::PAIR);
  }
  else if (force->pair) force->pair->compute_dummy(eflag,vflag);


  if (atomKK->molecular) {
    if (force->bond) {
      atomKK->sync(force->bond->execution_space,force->bond->datamask_read);
      force->bond->compute(eflag,vflag);
      atomKK->modified(force->bond->execution_space,force->bond->datamask_modify);
    }
    if (force->angle) {
      atomKK->sync(force->angle->execution_space,force->angle->datamask_read);
      force->angle->compute(eflag,vflag);
      atomKK->modified(force->angle->execution_space,force->angle->datamask_modify);
    }
    if (force->dihedral) {
      atomKK->sync(force->dihedral->execution_space,force->dihedral->datamask_read);
      force->dihedral->compute(eflag,vflag);
      atomKK->modified(force->dihedral->execution_space,force->dihedral->datamask_modify);
    }
    if (force->improper) {
      atomKK->sync(force->improper->execution_space,force->improper->datamask_read);
      force->improper->compute(eflag,vflag);
      atomKK->modified(force->improper->execution_space,force->improper->datamask_modify);
    }
    timer->stamp(Timer::BOND);
  }

  if(force->kspace) {
    force->kspace->setup();
    if (kspace_compute_flag) {
      atomKK->sync(force->kspace->execution_space,force->kspace->datamask_read);
      force->kspace->compute(eflag,vflag);
      atomKK->modified(force->kspace->execution_space,force->kspace->datamask_modify);
      timer->stamp(Timer::KSPACE);
    } else force->kspace->compute_dummy(eflag,vflag);
  }

  if (force->newton) comm->reverse_comm();

  modify->setup(vflag);
  update->setupflag = 0;
}

#ifdef KOKKOS_ENABLE_OPENMP
void read_busytime(long *totals, long *idles, int n) {
  FILE *fp = fopen("/proc/stat", "r");
  char cpu_str[20];
  long s_user, s_nice, s_system, s_idle, s_iowait, s_irq, s_softirq, s_steal, s_guest, s_guest_nice;
  fscanf(fp, "%s %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld", cpu_str, &s_user, &s_nice, &s_system, &s_idle, &s_iowait, &s_irq, &s_softirq, &s_steal, &s_guest, &s_guest_nice);
  while (1) {
    int i;
    fscanf(fp, "%3s%d %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld", cpu_str, &i, &s_user, &s_nice, &s_system, &s_idle, &s_iowait, &s_irq, &s_softirq, &s_steal, &s_guest, &s_guest_nice);
    long total = s_user + s_nice + s_system + s_idle + s_iowait + s_irq + s_softirq + s_steal + s_guest + s_guest_nice;
    totals[i] = total;
    idles[i] = s_idle;
    if (i == n - 1) break;
  }
  fclose(fp);
}
#endif

/* ----------------------------------------------------------------------
   run for N steps
------------------------------------------------------------------------- */

void VerletKokkos::run(int n)
{
  bigint ntimestep;
  int nflag,sortflag;

  int n_post_integrate = modify->n_post_integrate;
  int n_pre_exchange = modify->n_pre_exchange;
  int n_pre_neighbor = modify->n_pre_neighbor;
  int n_pre_force = modify->n_pre_force;
  int n_pre_reverse = modify->n_pre_reverse;
  int n_post_force = modify->n_post_force;
  int n_end_of_step = modify->n_end_of_step;

  lmp->kokkos->auto_sync = 0;

  if (atomKK->sortfreq > 0) sortflag = 1;
  else sortflag = 0;

  f_merge_copy = DAT::t_f_array("VerletKokkos::f_merge_copy",atomKK->k_f.extent(0));

  atomKK->sync(Device,ALL_MASK);
  //static double time = 0.0;
  //Kokkos::Impl::Timer ktimer;

  int measure_busytime;
  char *s;
  s = getenv("LAMMPS_MEASURE_BUSYTIME");
  if (s) {
    measure_busytime = atoi(s);
  } else {
    measure_busytime = 0;
  }

#ifdef KOKKOS_ENABLE_OPENMP
  int n_cores = Kokkos::OpenMP::thread_pool_size();
  long *totals1, *totals2, *idles1, *idles2;
#endif
#ifdef KOKKOS_ENABLE_ARGOBOTS
  uint64_t t1;
#endif

  if (measure_busytime) {
#ifdef KOKKOS_ENABLE_OPENMP
    totals1 = (long*)malloc(sizeof(long) * n_cores);
    totals2 = (long*)malloc(sizeof(long) * n_cores);
    idles1 = (long*)malloc(sizeof(long) * n_cores);
    idles2 = (long*)malloc(sizeof(long) * n_cores);
    read_busytime(totals1, idles1, n_cores);
#endif
#ifdef KOKKOS_ENABLE_ARGOBOTS
    t1 = mlog_clock_gettime_in_nsec();
    for (int i = 0; i < Kokkos::Impl::g_num_xstreams; i++) {
      Kokkos::Impl::g_busytimes[i].begin_time = t1;
    }
#endif
  }

  timer->init_timeout();
  for (int i = 0; i < n; i++) {

    if (timer->check_timeout(i)) {
      update->nsteps = i;
      break;
    }
    ntimestep = ++update->ntimestep;
    ev_set(ntimestep);

    // initial time integration

    //ktimer.reset();
    timer->stamp();
    modify->initial_integrate(vflag);
    //time += ktimer.seconds();
    if (n_post_integrate) modify->post_integrate();
    timer->stamp(Timer::MODIFY);

    // regular communication vs neighbor list rebuild

    nflag = neighbor->decide();

    if (nflag == 0) {
      timer->stamp();
      comm->forward_comm();
      timer->stamp(Timer::COMM);
    } else {
      analysis_wait();
      // added debug
      //atomKK->sync(Host,ALL_MASK);
      //atomKK->modified(Host,ALL_MASK);

      if (n_pre_exchange) {
        timer->stamp();
        modify->pre_exchange();
        timer->stamp(Timer::MODIFY);
      }

      // debug
      //atomKK->sync(Host,ALL_MASK);
      //atomKK->modified(Host,ALL_MASK);
      if (triclinic) domain->x2lamda(atomKK->nlocal);
      domain->pbc();
      if (domain->box_change) {
        domain->reset_box();
        comm->setup();
        if (neighbor->style) neighbor->setup_bins();
      }
      timer->stamp();

      // added debug
      //atomKK->sync(Device,ALL_MASK);
      //atomKK->modified(Device,ALL_MASK);

      comm->exchange();
      if (sortflag && ntimestep >= atomKK->nextsort) atomKK->sort();
      comm->borders();

      // added debug
      //atomKK->sync(Host,ALL_MASK);
      //atomKK->modified(Host,ALL_MASK);

      if (triclinic) domain->lamda2x(atomKK->nlocal+atomKK->nghost);

      timer->stamp(Timer::COMM);
      if (n_pre_neighbor) {
        modify->pre_neighbor();
        timer->stamp(Timer::MODIFY);
      }
      neighbor->build(1);
      timer->stamp(Timer::NEIGH);
    }

    if (enable_analysis && i % analysis_intvl == 0) {
      analysis_wait();
      analysis((NeighListKokkos<LMPDeviceType>*)force->pair->list);
    }
    if (!async_analysis) {
      analysis_wait();
    }

    // force computations
    // important for pair to come before bonded contributions
    // since some bonded potentials tally pairwise energy/virial
    // and Pair:ev_tally() needs to be called before any tallying

    force_clear();

    timer->stamp();

    if (n_pre_force) {
      modify->pre_force(vflag);
      timer->stamp(Timer::MODIFY);
    }

    bool execute_on_host = false;
    unsigned int datamask_read_device = 0;
    unsigned int datamask_modify_device = 0;
    unsigned int datamask_read_host = 0;

    if ( pair_compute_flag ) {
      if (force->pair->execution_space==Host) {
        execute_on_host  = true;
        datamask_read_host   |= force->pair->datamask_read;
        datamask_modify_device |= force->pair->datamask_modify;
      } else {
        datamask_read_device   |= force->pair->datamask_read;
        datamask_modify_device |= force->pair->datamask_modify;
      }
    }
    if ( atomKK->molecular && force->bond )  {
      if (force->bond->execution_space==Host) {
        execute_on_host  = true;
        datamask_read_host   |= force->bond->datamask_read;
        datamask_modify_device |= force->bond->datamask_modify;
      } else {
        datamask_read_device   |= force->bond->datamask_read;
        datamask_modify_device |= force->bond->datamask_modify;
      }
    }
    if ( atomKK->molecular && force->angle ) {
      if (force->angle->execution_space==Host) {
        execute_on_host  = true;
        datamask_read_host   |= force->angle->datamask_read;
        datamask_modify_device |= force->angle->datamask_modify;
      } else {
        datamask_read_device   |= force->angle->datamask_read;
        datamask_modify_device |= force->angle->datamask_modify;
      }
    }
    if ( atomKK->molecular && force->dihedral ) {
      if (force->dihedral->execution_space==Host) {
        execute_on_host  = true;
        datamask_read_host   |= force->dihedral->datamask_read;
        datamask_modify_device |= force->dihedral->datamask_modify;
      } else {
        datamask_read_device   |= force->dihedral->datamask_read;
        datamask_modify_device |= force->dihedral->datamask_modify;
      }
    }
    if ( atomKK->molecular && force->improper ) {
      if (force->improper->execution_space==Host) {
        execute_on_host  = true;
        datamask_read_host   |= force->improper->datamask_read;
        datamask_modify_device |= force->improper->datamask_modify;
      } else {
        datamask_read_device   |= force->improper->datamask_read;
        datamask_modify_device |= force->improper->datamask_modify;
      }
    }
    if ( kspace_compute_flag ) {
      if (force->kspace->execution_space==Host) {
        execute_on_host  = true;
        datamask_read_host   |= force->kspace->datamask_read;
        datamask_modify_device |= force->kspace->datamask_modify;
      } else {
        datamask_read_device   |= force->kspace->datamask_read;
        datamask_modify_device |= force->kspace->datamask_modify;
      }
    }


    if (pair_compute_flag) {
      atomKK->sync(force->pair->execution_space,force->pair->datamask_read);
      atomKK->sync(force->pair->execution_space,~(~force->pair->datamask_read|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
      Kokkos::Impl::Timer ktimer;
      force->pair->compute(eflag,vflag);
      atomKK->modified(force->pair->execution_space,force->pair->datamask_modify);
      atomKK->modified(force->pair->execution_space,~(~force->pair->datamask_modify|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
      timer->stamp(Timer::PAIR);
    }

      if(execute_on_host) {
        if(pair_compute_flag && force->pair->datamask_modify!=(F_MASK | ENERGY_MASK | VIRIAL_MASK))
          Kokkos::fence();
        atomKK->sync_overlapping_device(Host,~(~datamask_read_host|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
        if(pair_compute_flag && force->pair->execution_space!=Host) {
          Kokkos::deep_copy(LMPHostType(),atomKK->k_f.h_view,0.0);
        }
    }

    if (atomKK->molecular) {
      if (force->bond) {
        atomKK->sync(force->bond->execution_space,~(~force->bond->datamask_read|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
        force->bond->compute(eflag,vflag);
        atomKK->modified(force->bond->execution_space,~(~force->bond->datamask_modify|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
      }
      if (force->angle) {
        atomKK->sync(force->angle->execution_space,~(~force->angle->datamask_read|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
        force->angle->compute(eflag,vflag);
        atomKK->modified(force->angle->execution_space,~(~force->angle->datamask_modify|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
      }
      if (force->dihedral) {
        atomKK->sync(force->dihedral->execution_space,~(~force->dihedral->datamask_read|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
        force->dihedral->compute(eflag,vflag);
        atomKK->modified(force->dihedral->execution_space,~(~force->dihedral->datamask_modify|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
      }
      if (force->improper) {
        atomKK->sync(force->improper->execution_space,~(~force->improper->datamask_read|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
        force->improper->compute(eflag,vflag);
        atomKK->modified(force->improper->execution_space,~(~force->improper->datamask_modify|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
      }
      timer->stamp(Timer::BOND);
    }

    if (kspace_compute_flag) {
      atomKK->sync(force->kspace->execution_space,~(~force->kspace->datamask_read|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
      force->kspace->compute(eflag,vflag);
      atomKK->modified(force->kspace->execution_space,~(~force->kspace->datamask_modify|(F_MASK | ENERGY_MASK | VIRIAL_MASK)));
      timer->stamp(Timer::KSPACE);
    }

    if(execute_on_host && !std::is_same<LMPHostType,LMPDeviceType>::value) {
      if(f_merge_copy.extent(0)<atomKK->k_f.extent(0)) {
        f_merge_copy = DAT::t_f_array("VerletKokkos::f_merge_copy",atomKK->k_f.extent(0));
      }
      f = atomKK->k_f.d_view;
      Kokkos::deep_copy(LMPHostType(),f_merge_copy,atomKK->k_f.h_view);
      Kokkos::parallel_for(atomKK->k_f.extent(0),
        ForceAdder<DAT::t_f_array,DAT::t_f_array>(atomKK->k_f.d_view,f_merge_copy));
      atomKK->k_f.clear_sync_state(); // special case
      atomKK->k_f.modify<LMPDeviceType>();
    }

    if (n_pre_reverse) {
      modify->pre_reverse(eflag,vflag);
      timer->stamp(Timer::MODIFY);
    }

    // reverse communication of forces

    if (force->newton) {
      Kokkos::fence();
      comm->reverse_comm();
      timer->stamp(Timer::COMM);
    }

    // force modifications, final time integration, diagnostics

    if (n_post_force) modify->post_force(vflag);
    modify->final_integrate();
    if (n_end_of_step) modify->end_of_step();
    timer->stamp(Timer::MODIFY);

    // all output

    if (ntimestep == output->next) {
       atomKK->sync(Host,ALL_MASK);

      timer->stamp();
      output->write(ntimestep);
      timer->stamp(Timer::OUTPUT);
    }
  }

  analysis_wait();

  if (measure_busytime) {
#ifdef KOKKOS_ENABLE_OPENMP
    MPI_Barrier(MPI_COMM_WORLD);

    read_busytime(totals2, idles2, n_cores);

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    for (int i = 0; i < n_cores; i++) {
      long total_diff = totals2[i] - totals1[i];
      long idle_diff = idles2[i] - idles1[i];
      double idleness = (double)idle_diff / total_diff;
      printf("[rank %d, worker %d] busy ratio: %f\n", rank, i, 1.0 - idleness);
    }
#endif
#ifdef KOKKOS_ENABLE_ARGOBOTS
    MPI_Barrier(MPI_COMM_WORLD);

    uint64_t t2 = mlog_clock_gettime_in_nsec();

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    Kokkos::Impl::g_busytimes[0].acc += t2 - Kokkos::Impl::g_busytimes[0].last_time;
    for (int i = 0; i < Kokkos::Impl::g_num_xstreams; i++) {
      uint64_t acc = Kokkos::Impl::g_busytimes[i].acc;
      printf("[rank %d, worker %d] busy ratio: %f\n", rank, i, (double)acc / (t2 - t1));
    }
#endif
  }

  free(threads);
  free(ts);
  free(bond_counts);

  atomKK->sync(Host,ALL_MASK);
  lmp->kokkos->auto_sync = 1;
}

/* ----------------------------------------------------------------------
   clear force on own & ghost atoms
   clear other arrays as needed
------------------------------------------------------------------------- */

void VerletKokkos::force_clear()
{
  if (external_force_clear) return;

  atomKK->k_f.clear_sync_state(); // ignore host forces/torques since device views
  atomKK->k_torque.clear_sync_state(); //   will be cleared below

  // clear force on all particles
  // if either newton flag is set, also include ghosts
  // when using threads always clear all forces.

  if (neighbor->includegroup == 0) {
    int nall = atomKK->nlocal;
    if (force->newton) nall += atomKK->nghost;

    Kokkos::parallel_for(nall, Zero<typename ArrayTypes<LMPDeviceType>::t_f_array>(atomKK->k_f.view<LMPDeviceType>()));
    atomKK->modified(Device,F_MASK);

    if (torqueflag) {
      Kokkos::parallel_for(nall, Zero<typename ArrayTypes<LMPDeviceType>::t_f_array>(atomKK->k_torque.view<LMPDeviceType>()));
      atomKK->modified(Device,TORQUE_MASK);
    }

  // neighbor includegroup flag is set
  // clear force only on initial nfirst particles
  // if either newton flag is set, also include ghosts

  } else {
    Kokkos::parallel_for(atomKK->nfirst, Zero<typename ArrayTypes<LMPDeviceType>::t_f_array>(atomKK->k_f.view<LMPDeviceType>()));
    atomKK->modified(Device,F_MASK);

    if (torqueflag) {
      Kokkos::parallel_for(atomKK->nfirst, Zero<typename ArrayTypes<LMPDeviceType>::t_f_array>(atomKK->k_torque.view<LMPDeviceType>()));
      atomKK->modified(Device,TORQUE_MASK);
    }

    if (force->newton) {
      auto range = Kokkos::RangePolicy<LMPDeviceType>(atomKK->nlocal, atomKK->nlocal + atomKK->nghost);
      Kokkos::parallel_for(range, Zero<typename ArrayTypes<LMPDeviceType>::t_f_array>(atomKK->k_f.view<LMPDeviceType>()));
      atomKK->modified(Device,F_MASK);

      if (torqueflag) {
	Kokkos::parallel_for(range, Zero<typename ArrayTypes<LMPDeviceType>::t_f_array>(atomKK->k_torque.view<LMPDeviceType>()));
	atomKK->modified(Device,TORQUE_MASK);
      }
    }
  }
}
