# Benchmark methodology

## Purpose

These benchmarks establish the Phase 6 single-threaded baseline. They are not
optimization claims. Future performance changes must rerun the same workloads
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
