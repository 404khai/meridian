# Benchmark methodology

## Purpose

These benchmarks establish the single-threaded baseline and its measured
matching optimization comparison. Future performance changes must rerun the same workloads
on comparable hardware and include before/after results.

## Build and run

Use an optimized build with assertions and sanitizers disabled:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target meridian_benchmarks --parallel
./build-release/meridian_benchmarks \
  --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=build-release/benchmark-results.json \
  --benchmark_out_format=json
```

Run on an otherwise idle machine. Record the CPU, operating system, compiler,
build type, commit, and Google Benchmark version alongside results. Wall-clock
and CPU frequency variation means results from different machines are not
directly comparable.

## Workloads

- `OrderBook/Insert/SingleLevel`: append 1,000 or 10,000 orders at one price.
- `OrderBook/Insert/ManyLevels`: insert across 1,000 distinct prices.
- `OrderBook/Lookup/Hit` and `Miss`: query a 10,000-order ID index.
- `OrderBook/Cancel/SingleLevel`: cancel 1,000 or 10,000 indexed orders.
- `Matching/MarketSweep/SingleLevel`: sweep 100 or 1,000 FIFO resting orders.
- `Matching/MarketSweep/ManyLevels`: sweep 100 or 1,000 orders across 100 prices.

Fixture creation is paused for cancellation and matching. Timed work retains
normal engine behavior, including event generation and fill construction.
Insertion includes price-level/index allocation and accepted-event generation.
Reported `items_per_second` treats one inserted, looked-up, cancelled, or
matched order as one item.

The current implementation has one candidate storage design: ordered maps for
price levels, FIFO lists within a level, and an unordered order-ID index.
Alternative data structures belong to later benchmark-backed optimization
work; this suite is their comparison baseline.

## Matching optimization comparison

For the 2026-10-26 comparison, run the unchanged suite at both revisions with
`--benchmark_min_time=0.2s --benchmark_repetitions=5` and the JSON output flags
above. The optimized matching cases were additionally confirmed with
`--benchmark_filter=Matching/ --benchmark_min_time=0.5s
--benchmark_repetitions=7`. Run measurements serially, with builds and tests
outside the timed runs. Retain all results, including noisy samples. Batch CPU
time divided by the order count gives per-match time; these are aggregate
throughput measurements, not individual-order latency percentiles.

## Units and interpretation

Google Benchmark reports CPU and wall times per benchmark iteration. Insert,
cancel, and sweep iterations process a whole batch; divide median CPU time by
its item count for the per-item table. Lookup iterations perform one query.
Throughput counters use inserted/looked-up/cancelled/matched orders, not submitted
aggressors: a 1000-maker sweep is one submission but 1000 measured items.
A median of batch averages is not a p50 individual-order latency, and aggregate
repetition variability is not a p99 tail. Timing pauses exclude fixture work
but not normal fill/event generation. No CPU-affinity guarantee exists on the
recorded macOS host.

The optimized matcher is source revision `29140e8`; the paired baseline is
`8dc62b3`. Full tables retain unchanged operations and noisy/regressing cases,
not only improvements. The longer confirmation uses different repetition/time
settings and is distinguished from the like-for-like comparison. The original
September baseline remains historical; its OS differs from the October runs.
Raw JSON is linked in [benchmark-results.md](benchmark-results.md).

For individual submit/completion latency under verified load, use the separate
[stress methodology](stress-testing.md) and [results](stress-results.md). Those
closed-loop workloads include validation overhead in throughput and histogram
quantization in percentiles. No storage/allocator alternative has been measured,
and market-data or TCP throughput has not been characterized by these suites.
