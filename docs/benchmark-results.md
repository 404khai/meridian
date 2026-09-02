# Benchmark results

## Phase 6 single-threaded baseline

Recorded on 2026-09-02 from benchmark source commit `4852342`.

- Machine: Apple M3 MacBook Pro, 8 physical/logical cores
- Operating system: macOS 26.5 (Darwin 25.5.0, arm64)
- Compiler: Apple Clang 21.0.0
- Build: CMake 4.3.4, `Release`, sanitizers disabled
- Harness: Google Benchmark 1.9.5, one benchmark thread
- Repetitions: five; table reports median CPU time

Google Benchmark could not read `hw.cpufrequency` or set thread affinity on
this macOS host. The reported 24 MHz metadata value is therefore invalid, but
the framework states that this does not affect measured benchmark times.

| Workload | Batch size | Median CPU time per item | Items/second |
| --- | ---: | ---: | ---: |
| Insert, single level | 1,000 | 58.62 ns | 17.06 M |
| Insert, single level | 10,000 | 55.74 ns | 17.94 M |
| Insert, 1,000 levels | 1,000 | 123.88 ns | 8.07 M |
| Insert, 1,000 levels | 10,000 | 96.83 ns | 10.33 M |
| Lookup hit, 10,000-order book | 1 | 3.78 ns | 264.89 M |
| Lookup miss, 10,000-order book | 1 | 4.39 ns | 227.65 M |
| Cancel, single level | 1,000 | 47.74 ns | 20.95 M |
| Cancel, single level | 10,000 | 46.47 ns | 21.52 M |
| Market sweep, single level | 100 | 245.48 ns | 4.07 M |
| Market sweep, single level | 1,000 | 1,221.86 ns | 0.82 M |
| Market sweep, 100 levels | 100 | 173.03 ns | 5.78 M |
| Market sweep, 100 levels | 1,000 | 143.94 ns | 6.95 M |

The 10,000-order single-level insertion case had one noisy sample in the
five-repetition run (16.3% CPU-time coefficient of variation). Its table value
comes from a ten-repetition rerun: 557,449 ns median batch CPU time, 55.74 ns
per item, and 1.91% coefficient of variation. All other cases had CPU-time
variation at or below 3.21%.

## Baseline observations

- Indexed lookup and cancellation show stable constant-time behavior at the
  tested 10,000-order size.
- Insertion into one existing level is cheaper than insertion distributed
  across many ordered-map levels.
- The single-level 1,000-order sweep is substantially slower per match than
  the 100-order case. The current matcher calls `orders_at()` for each fill,
  which copies the remaining price-level queue; this workload exposes that
  scaling cost. It is recorded as a future optimization candidate, but Phase 6
  intentionally makes no performance-motivated production changes.

These values are the required comparison point for later performance work.
Results from other machines must be reported separately rather than compared
as if they were measured under the same conditions.
