# Artifact of "Lightweight Preemptive User-Level Threads" in PPoPP'21

This is the artifact of the following paper:

Shumpei Shiina, Shintaro Iwasaki, Kenjiro Taura, and Pavan Balaji, Lightweight Preemptive User-Level Threads, The 26th ACM SIGPLAN Symposium on Principles and Practice of Parallel Programming (PPoPP '21)

## Directories

All the necessary programs are included in this package.  No download (and thus no internet connection) is necessary except for preliminary software.

- `argobots`
    - Preemptive M:N thread library based on Argobots (v1.0rc1)
- `bolt`
    - BOLT (v1.0rc3) with a little modification to enable preemption
    - A fair scheduler for HPGMG is also implemented
- `preemption_benchmarks`
    - OS interrupt and preemption overhead measurement
- `chol`
    - Cholesky decomposition kernel extracted from SLATE
- `hpgmg`
    - HPGMG-FV version 0.4 with a little modification for performance measurement
- `lammps`
    - LAMMPS (stable_7Aug2019) with experimental in situ analysis implementation with Argobots
    - An Argobots backend is implemented in Kokkos
- `raw_results`
    - Contains raw results obtained in our environments.  We used this data for the paper

## Preliminary

Those packages must be available on compute nodes.

- Intel compiler, OpenMP, and MPI
    - We checked the results with version 19.0.4.243
- Basic compilation tools (CMake, autotools, GNU Make, ...)
- Singularity
    - Only for graph plotting

Fix the frequency and disable turbo boost if possible.

## Setup

Modify `envs.bash` to change the path to Intel compilers, the number of cores, and the number of sockets.

```
# location of Intel compiler package
export INTEL_DIR=/soft/compilers/intel-2019/compilers_and_libraries_2019.4.243/linux

# machine config: number of cores per node
export N_CORES=56

# machine config: number of sockets per node
export N_SOCKETS=2

# root directory, which must point to the top directory of the artifact
ROOT_DIR=$(cd $(dirname -- $0) && pwd -P)

# install directory (default: ROOT_DIR/opt)
INSTALL_DIR=${ROOT_DIR}/opt
```

To check the configurations, try
```
bash envs.bash
```
and see if no error happens.

## OS Interrupt Measurement

Run:
```
./measure_interrupt.bash
```

The script will compile the benchmark and dependent libraries (for the first execution), run it, and save execution logs.

It will take tens of minutes to be completed.  After running the script, a plot file (`interrupt_plot.html`) will be created.
You can see the plot by opening the file in your browser.  The results are corresponding to Figure X in the paper.

### Customization of this test

You may want to modify `THREADS` variable in `preemption_benchmarks/measure_jobs/timer_measure.bash` to match your machine configuration.
To get more reliable result, set 10 to `N` in the script.

If you want to further customize the test, please modify `preemption_benchmarks/measure_jobs/timer_measure.bash`. The following arguments are supported.
```
Usage: ./timer_measure [args]
  Parameters:
    -n : # of threads (int)
    -t : # of timers (int)
    -i : Interval time of preemption in usec (int)
    -p : # of preemption for each thread
    -m : Managed timer (0 or 1)
    -s : Sync timer (0 or 1)
    -c : Chain wake-up (0 or 1)
    -d : Thread timer (0 or 1)
    -f : Use timerfd (0 or 1)
```

## Overhead Measurement

Run:
```
./measure_overhead.bash
```

It will take several minutes to be completed, and a plot file (`overhead_plot.html`) will be output.

Set `NREPEAT=10` in `preemption_benchmarks/measure_jobs/noop.bash` to get more stable results (which will take several hours).
See `preemption_benchmarks/README.md` for more details.

You can also run:
```
./measure_overhead_direct.bash
```
and see the directly measured overhead of preemption (no plot is generated).

## Cholesky Decomposition

Run:
```
./measure_chol.bash
```

It will take tens of minutes to be completed, and a plot file (`chol_plot.html`) will be output.

You can change the number of iterations through `REPEAT` variable in `chol/run.bash`.
Although it shuold work with the latest Intel compiler suite, this benchmark partially depends on the version of Intel OpenMP and MKL because of the reverse-engineering patch (see Section X in the paper for details).
If it hangs, please pass `-X` to `./measure_chol.bash` to skip tests that rely on this MKL patch.

## HPGMG

Run:
```
./measure_hpgmg.bash
```

It will take tens of minutes to be completed, and a plot file (`hpgmg_plot.html`) will be output.

`N_SOCKETS` in `envs.bash` is the number of MPI processes used in HPGMG-FV.
It assumes execution on one node.

Change `N_WORKERS` in `hpgmg/measure_mpi.sh` to increase the number of data points (e.g., `$(seq 4 $NTHREADS)` is used in our experiments).
In that case, `n_workers` in `hpgmg/plot.exs` should be also changed for plotting (e.g., `:lists.seq(4, n_threads, 1)` to plot all points).

## LAMMPS

It assumes using multiple nodes for computation (e.g., 4 nodes in our experiments); you may need to set some environment variables to use multiple nodes by `mpirun`, probably through a job management system.

Run:
```
./measure_lammps.bash
```

It will take tens of minutes to be completed, and a plot file (`lammps_plot.html`) will be output.
To choose a different analysis interval (1 or 2) to plot, change the `intvl` variable in `lammps/plot_size.exs` and run:

```
cd lammps
../plot.bash plot_size.exs ../lammps_plot.html
```

Increase `N_REPEATS` in `lammps/measure.bash` to get more stable results (e.g., 10 in our experiments).
