# Preemption Benchmarks

This README explains only the "noop" benchmark, which measures the overhead of preemption.

## Run

For example, you can measure the performance of "noop" benchmark by running:
```
./measure_jobs/job_common.bash ./measure_jobs/noop.bash
```

Configs for measurement exist in the head of `./measure_jobs/noop.bash`.
Comment or uncomment settings as you want.

1. `EXEC_TYPE=no_preemption` as the baseline
2. `EXEC_TYPE=preemption` and `PREEMPTION_TYPE=disabled` for nonpreemptive threads
3. `EXEC_TYPE=preemption` and `PREEMPTION_TYPE=yield` for signal-yield
4. `EXEC_TYPE=preemption` and `PREEMPTION_TYPE=klts_futex_local` for **optimized** KLT-switching

For unoptimized version of KLT-switching, you need to rebuild Argobots with different compile flags.
Uncomment the following lines in `setup.bash` to disable local pool or futex optimizations and run `setup.bash` again to recompile Argobots.
```
# CFLAGS="$CFLAGS -DABT_SUB_XSTREAM_LOCAL_POOL=0" # disable using local pools in KLT-switching
# CFLAGS="$CFLAGS -DABT_USE_FUTEX_ON_SLEEP=0" # disable futex in KLT-switching
```

5. After you set `ABT_SUB_XSTREAM_LOCAL_POOL=0`, run measurement with `EXEC_TYPE=preemption` and `PREEMPTION_TYPE=klts_futex` for futex-only KLT-switching
6. After you set `ABT_SUB_XSTREAM_LOCAL_POOL=0` and `ABT_USE_FUTEX_ON_SLEEP=0`, run measurement with `EXEC_TYPE=preemption` and `PREEMPTION_TYPE=klts` for unoptimized KLT-switching

Notes:
- It is better to change `PREFIX` based on the architecture (e.g., `PREFIX=noop_knl` on KNL machine).
- Remember to change `NWORKERS` to use all cores in the target architecture.
- `NLOOP` depends on architectures; it is chosen so that one iteration takes about 1s with nonpreemptive threads.
- Skip too small timer intervals by changing `PREEMPTION_INTVLS` if they take too much time (e.g., on KNL).
- `NREPEATS=10` would take very long time; it is better to use a smaller number for the first try.

## Plot

```
../plot.bash ./plot/noop_plot.exs noop.html
```
will output to `noop.html`.
Then you can open `noop.html` in your browser to see the generated plot.
(It internally uses Singularity to use Plotly)

You may need to modify the first few lines in `plot/noop_plot.exs` to change the prefix and plotting configuration.
