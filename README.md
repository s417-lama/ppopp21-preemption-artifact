# Lightweight Preemptive User-Level Threads

## Directories

- `argobots`
    - Preemptive M:N thread library based on Argobots (v1.0rc1)
- `bolt`
    - BOLT (v1.0rc3) with a little modification to enable preemption
    - A fair scheduler for HPGMG is also implemented
- `preemption_benchmarks`
    - Overhead measurement for preemptive threads
- `chol`
    - Cholesky decomposition kernel extracted from SLATE
- `hpgmg`
    - HPGMG-FV version 0.4 with a little modification for performance measurement
- `lammps`
    - LAMMPS (stable_7Aug2019) with experimental in situ analysis implementation with Argobots
    - An Argobots backend is implemented in Kokkos
- `raw_results`
    - Contains raw results obtained in our environments

## Preliminary

- Intel compiler, OpenMP, and MPI
    - We checked the results only with version 19.0.4.243
- Singularity
    - Only for graph plotting

Fix the frequency and disable turbo boost if possible.

## Setup

Modify `envs.bash` to change the path to Intel compilers and the number of cores.

Try
```
source envs.bash
```
to check if no error happens.

## Overhead Measurement

Run:
```
./measure_overhead.bash
```

It will take several minutes to be completed, and a plot file (`overhead_plot.html`) will be output.
You can see the plot in your browser by opening the file.

Set `NREPEAT=10` in `preemption_benchmarks/measure_jobs/noop.bash` to get more stable results (which will take several hours).
See `preemption_benchmarks/README.md` for more details.

## Cholesky Decomposition

Run:
```
./measure_chol.bash
```

It will take tens of minutes to be completed, and a plot file (`chol_plot.html`) will be output.

You can change the number of iterations through `REPEAT` variable in `chol/run.bash`.

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

Run:
```
./measure_lammps.bash
```

It will take tens of minutes to be completed, and a plot file (`lammps_plot.html`) will be output.

Increase `N_REPEATS` in `lammps/measure.bash` to get more stable results (e.g., 10 in our experiments).
