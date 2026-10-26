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

## Historical baseline observations

- Indexed lookup and cancellation show stable constant-time behavior at the
  tested 10,000-order size.
- Insertion into one existing level is cheaper than insertion distributed
  across many ordered-map levels.
- The single-level 1,000-order sweep is substantially slower per match than
  the 100-order case. The baseline matcher called `orders_at()` for each fill,
  which copies the remaining price-level queue; this workload exposes that
  scaling cost. This identified the optimization candidate addressed in the October
  comparison below; the original baseline intentionally changed no production code.

These values are the required comparison point for later performance work.
Results from other machines must be reported separately rather than compared
as if they were measured under the same conditions.

## Matching sweep optimization — 2026-10-26

Before: `8dc62b3` (merged concurrent-ingestion baseline). Optimized source:
`29140e8a7029e066301b8f507c02c859bc51cd22`. The harness is unchanged.

- Machine: Apple M3, 8 cores; macOS 27.0 (26A428), arm64
- Compiler: Apple Clang 21.0.0 (clang-2100.3.34.2)
- Build: CMake Release, warnings as errors, sanitizers disabled
- Harness: pinned Google Benchmark 1.9.5, one thread
- Paired full-suite runs: five repetitions, minimum time 0.2 seconds
- Matching confirmation: seven repetitions, minimum time 0.5 seconds

This is a fresh before/after comparison on the same host and OS. The older
baseline above ran on a different OS version and is retained for history.
Times below are median batch CPU nanoseconds divided by items per batch.

| Workload | Items/batch | Before ns/item | After ns/item | Before/after |
| --- | ---: | ---: | ---: | ---: |
| Matching/MarketSweep/SingleLevel | 100 | 313.59 | 95.90 | 3.27x |
| Matching/MarketSweep/SingleLevel | 1,000 | 2086.27 | 72.17 | 28.91x |
| Matching/MarketSweep/ManyLevels | 100 | 153.42 | 164.94 | 0.93x |
| Matching/MarketSweep/ManyLevels | 1,000 | 121.26 | 60.69 | 2.00x |
| OrderBook/Insert/SingleLevel | 1,000 | 53.31 | 53.99 | 0.99x |
| OrderBook/Insert/SingleLevel | 10,000 | 50.10 | 50.91 | 0.98x |
| OrderBook/Insert/ManyLevels | 1,000 | 116.67 | 118.33 | 0.99x |
| OrderBook/Insert/ManyLevels | 10,000 | 89.35 | 91.00 | 0.98x |
| OrderBook/Lookup/Hit | 1 | 3.80 | 3.77 | 1.01x |
| OrderBook/Lookup/Miss | 1 | 6.54 | 4.47 | 1.46x |
| OrderBook/Cancel/SingleLevel | 1,000 | 45.35 | 39.98 | 1.13x |
| OrderBook/Cancel/SingleLevel | 10,000 | 52.24 | 39.54 | 1.32x |

The 1,000-maker single-level sweep improves from 2.086 ms to 72.17 µs in
the identical five-repetition runs (28.91x). The longer confirmation run
measures 52.79 µs (39.52x relative to the measured baseline). A single-level
sweep now copies N maker values rather than N(N+1)/2 and performs no temporary
level-vector allocations. Existing fill/event allocations remain.

| Confirmation workload | Median CPU ns/item | CPU-time CV |
| --- | ---: | ---: |
| Matching/MarketSweep/SingleLevel/100 | 77.49 | 13.27% |
| Matching/MarketSweep/SingleLevel/1000 | 52.79 | 0.98% |
| Matching/MarketSweep/ManyLevels/100 | 109.37 | 6.74% |
| Matching/MarketSweep/ManyLevels/1000 | 62.59 | 5.78% |

Measurement limits: this shared development host had variable background load.
The first optimized matching run had CPU-time CVs of 21.18%, 13.60%, 12.47%,
and 1.14% in table order; the confirmation improves confidence in the large
single-level result (0.98% CV). The initial 100-order many-level case regressed
7.51%, while confirmation improved; treat small-workload differences cautiously.
Baseline lookup-miss and cancellation cases were also noisy (12.98–20.81% CV);
no speedup is claimed for unchanged book operations. Insertion medians differ
by less than 2%. No latency percentiles or concurrent-throughput gains are
claimed. Google Benchmark still cannot read CPU frequency or set affinity on
this host; its 24 MHz metadata is invalid and is not used in these comparisons.

All raw JSON outputs, including initial noisy measurements:

- [Before](benchmarks/2026-10-26-sweep-before.json)
- [After, identical settings](benchmarks/2026-10-26-sweep-after.json)
- [After, longer confirmation](benchmarks/2026-10-26-sweep-after-confirm.json)

Validation: all seven CTest tests pass in Release, Debug, ASan/UBSan, and TSan.
The added two-sided sweep regression checks FIFO fills and every matched/trade
event after maker removal, a partial final maker, and final-level deletion.
