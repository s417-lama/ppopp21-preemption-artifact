# Artifact of "Lightweight Preemptive User-Level Threads" in PPoPP'21

This is the artifact of the following paper:

Shumpei Shiina, Shintaro Iwasaki, Kenjiro Taura, and Pavan Balaji, Lightweight Preemptive User-Level Threads, The 26th ACM SIGPLAN Symposium on Principles and Practice of Parallel Programming (PPoPP '21)

## Directories

All the necessary programs are included in this package.  No download (and thus no internet connection) is necessary except for preliminary software.

- `argobots`
    - Preemptive M:N thread library based on Argobots (v1.0rc1)
    - Original repository: https://github.com/pmodels/argobots
- `bolt`
    - BOLT (v1.0rc3) with a little modification to enable preemption
    - A fair scheduler for HPGMG is also implemented
    - Original repository: https://github.com/pmodels/bolt
- `preemption_benchmarks`
    - OS interrupt and preemption overhead measurement
- `chol`
    - Cholesky decomposition kernel extracted from SLATE
    - Original repository: https://bitbucket.org/icl/slate
- `hpgmg`
    - HPGMG-FV version 0.4 with a little modification for performance measurement
    - Original repository: https://github.com/hpgmg/hpgmg
- `lammps`
    - LAMMPS (stable_7Aug2019) with experimental in situ analysis implementation with Argobots
    - An Argobots backend is implemented in Kokkos
    - Original repository: https://github.com/lammps/lammps
- `raw_results`
    - Contains raw results obtained in our environments.  We used this data for the paper

## Preliminary

Those packages must be available on compute nodes.

- Intel compiler, OpenMP, and MPI
    - We checked the results with version 19.0.4.243
    - Although some benchmarks can run with gcc, it is better to use the Intel compiler suite if possible, especially for application evaluations (chol, hpgmg, lammps), which can be highly affected by the perfomance of OpenMP and MPI
- Basic compilation tools (CMake, autotools, GNU Make, ...)
- Singularity or Docker (not strictly required)
    - We used containers for graph plotting. If your compute nodes do not have `singularity` or `docker` command, you might want to install singularity or docker on your local system to plot graphs. See `--no-plot` and `--only-plot` options described later.

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
export INSTALL_DIR=${ROOT_DIR}/opt
```

To check the configurations, try
```
bash envs.bash
```
and see if no error happens.

Some of the measurements do not require Intel compilers and can run with gcc:
```
./measure_XXX --use-gcc
```
where `measure_XXX` is a measurement script explained in the following sections.

If an error is shown with `--use-gcc` option, the measurement must run with Intel compilers (some may be able to run with gcc by fixing some variables; see each error msg).

## OS Interrupt Measurement

Run:
```
./measure_interrupt.bash [OPTIONAL: --no-plot | --only-plot | --use-gcc]
```

The script will compile the benchmark and dependent libraries (for the first execution), run it, and save execution logs.

It will take a few minutes to be completed.  After running the script, a plot file (`interrupt_plot.html`) will be created.
You can see the plot by opening the file in your browser.  The results are corresponding to Figure 4 in the paper.

You might encounter the massage below at the end of execution:
```
Note: singularity or docker must be installed to generate plots.

To generate plots locally, download the 'results' dir in each subdirectory
(e.g., 'preemption_benchmarks/results') by 'scp -r' command and run
'./measure_XXX --only-plots' in your machine with singularity or docker installed.
```

To generate plots, `singularity` or `docker` command must be avaliable.
If no error occurs before the above message, the raw results should be stored in `results` dir in each subdirectory.
Please download the `results` dir to your local machine, place it to the same directory as the remote one, and run the measurement script with `--only-plot` option.
The above message will disappear with `--no-plot` option, but even without it, the results will be collected normally.

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

## Relative Overhead Measurement

Run:
```
./measure_overhead.bash [OPTIONAL: --no-plot | --only-plot | --use-gcc]
```

It will take several minutes to be completed, and a plot file (`overhead_plot.html`) will be output.
The results are corresponding to Figure 6 in the paper.

### Customization of this test

Set `NREPEAT=10` in `preemption_benchmarks/measure_jobs/noop.bash` to get more stable results (which will take several hours).
See `preemption_benchmarks/README.md` for more details.

## Absolute Overhead Measurement

Run:
```
./measure_overhead_direct.bash [OPTIONAL: --use-gcc]
```

It will show the directly measured overhead of preemption (no plot is generated).
The results are corresponding to Table 1 in the paper.

### Customization of this test

If you want to further customize the test, please modify `preemption_benchmarks/run_jobs/preemption_cost2.bash`.

Compile-time flags:
- `USE_PTHREAD=1`: measures Pthread's preemption overheads
- `PREEMPTION_TYPE=ABT_PREEMPTION_YIELD`: measures signal-yield's preemption overheads (in Argobots)
- `PREEMPTION_TYPE=ABT_PREEMPTION_NEW_ES`: measures KLT-switching's preemption overheads (in Argobots)

The following arguments are supported at runtime.
```
Usage: ./preemption_cost2 [args]
  Parameters:
    -x : # of ESs (int)
    -r : # of repeats (int)
    -g : # of preemption groups (int)
    -t : Preemption timer interval in usec (int)
    -p : # of preemption for each thread
```

## Cholesky Decomposition

Run:
```
./measure_chol.bash [OPTIONAL: --no-plot | --only-plot]
```

It will take tens of minutes to be completed, and a plot file (`chol_plot.html`) will be output.
The results are corresponding to Figure 7 in the paper.

Although it should work with the latest Intel compiler suite, this benchmark partially depends on the version of Intel MKL and the CPU architecture to use because of the reverse-engineering patch (see Section 4.1 in the paper for details).
Although it may hang or crash depending on the environment, the problem should be specific to this experiment; you can run other experiments instead.

### Customization of this test

If you want to further customize the test, please modify `chol/run.bash`.
The following parameters are ready to be customized.

```
# Size of each tile (NB x NB)
NB=1000

# Lookahead depth for prioritizing the critical path; see the SLATE paper [Gates+ SC19] for details
LOOKAHEAD=8

# Outer parallelism (the number of threads in the outer parallel loop)
OUTER=8

# Inner parallelism (the number of threads spawned at each Intel MKL subroutine)
INNER=8

# The number of nested threads spawned at lookahead updates; see the SLATE paper [Gates+ SC19] for details
SYRK_THREADS=8

# The number of repeats
REPEAT=10

# The number of warmup runs (performance results are not shown during warmup runs)
WARMUP=2

# Problem sizes (the number of tiles: n x n)
SIZES=(8 12 16 20 24)
```

You can also modify some OpenMP environment variables in the same file.

## HPGMG

Run:
```
./measure_hpgmg.bash [OPTIONAL: --no-plot | --only-plot]
```

It will take tens of minutes to be completed, and a plot file (`hpgmg_plot.html`) will be output.
The results are corresponding to Figure 8 in the paper.

`N_SOCKETS` in `envs.bash` is the number of MPI processes used in HPGMG-FV.
It assumes execution on one node.

### Customization of this test

Change `N_WORKERS` in `hpgmg/measure_mpi.sh` to increase the number of data points (e.g., `$(seq 4 $NTHREADS)` is used in our experiments).
In that case, `n_workers` in `hpgmg/plot.exs` should be also changed for plotting (e.g., `:lists.seq(4, n_threads, 1)` to plot all points).

Also, you can easily change the problem size by the following parameters in `hpgmg/measure_mpi.sh`:
```
LOG2DIM=8
NBOX=8
```
which are later passed to HPGMG as `./build/bin/hpgmg-fv $LOG2DIM $NBOX` (see the original documentation of HPGMG for details about these parameters).

You may want to change `SLEEP_TIME` variable in `hpgmg/measure_mpi.sh` when you change the problem size.
After `SLEEP_TIME` (in seconds) elapses, the number of available cores are dynamically reduced, so it is required to make it happen before the main computation begins.

## LAMMPS

It assumes using multiple nodes for computation (e.g., 4 nodes in our experiments); you may need to set some environment variables to use multiple nodes by `mpirun`, probably through a job management system.

Run:
```
./measure_lammps.bash [OPTIONAL: --no-plot | --only-plot]
```

It will take tens of minutes to be completed, and a plot file (`lammps_plot.html`) will be output.
The results are corresponding to Figure 9 in the paper.

To choose a different analysis interval (1 or 2) to plot, change the `intvl` variable in `lammps/plot_size.exs` and run:

```
./measure_lammps.bash --only-plot
```

### Customization of this test

Increase `N_REPEATS` in `lammps/measure.bash` to get more stable results (e.g., 10 in our experiments).
Other variables in the script you can change are:

- `ANALYSIS_INTVLS`: how often analysis computation is triggered (larger is less often)
- `ANALYSIS_THREADS`: the number of analysis threads
- `SIZES`: problem sizes (the number of atoms is proportional to n^3)
