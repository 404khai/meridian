# Stress and soak validation

`meridian_stress` is a standard-library-only test executable, built when
`BUILD_TESTING=ON`. It does not change production matching, add logging to core,
or claim a performance optimization. Fixed-cycle cases run in normal CTest;
long runs are explicit or use the dedicated CI soak workflow.

## Workloads and correctness checks

A matching cycle submits 128 non-crossing makers, 2048 seeded mixed requests,
and two maximum-quantity market sweeps (2178 submissions). One matching engine
survives all cycles. Each cycle ends empty; its ID index and sequence counters
continue across cycles. Live resting state stays at or below 256 orders. Events
are drained and checked after every submission, keeping retained history bounded.

The seeded generator mixes buys/sells, limit/market orders, partial and multiple
fills, invalid enums, zero quantity, invalid prices, duplicate currently resting
IDs, and `uint64_t`-maximum sweep quantities. A separate unsorted-vector matcher
uses linear best-price selection and FIFO vector order. Every returned result
and every event is compared to this reference: exact acceptance/rejection fields,
rejection reason, fill IDs/price/quantity, and accepted/matched/trade sequence.
This detects missing, duplicated, reordered, and corrupted events, rather than
only comparing aggregate counts. The event drain is checked for repeat emission.
Periodic full book comparisons check ID lookup, remaining quantities, depth,
price levels, best prices, and FIFO. Every step checks order count and the
no-crossed-book invariant. Two seeded fixed runs and replay digests make failures
reproducible. Failure output identifies seed and 1-based cycle.

A concurrent cycle creates one ingestion adapter, seeds 256 makers, starts four
producers together, and completes 64 market submissions per producer. Each
producer has at most one request outstanding. Every maker ID must appear once
and only once in the fills; every future must complete with correct quantities,
and the final published snapshot must report 512 submissions and an empty book.
Thread exceptions are collected after join. Each cycle destroys/joins its adapter.
This repeatedly tests lifecycle reclamation while bounding its retained event
log. The existing adapter has no event-drain API: **exact event verification is
for the direct matcher; concurrent checks cover fills, completions, and snapshots**.
This is not a long-lived concurrent-adapter memory-growth claim.

When networking is enabled, `meridian_protocol_adversarial_tests` generates
20000 seeded invalid binary/control-byte requests, including overlong frames,
interleaves valid matches and queries, and checks final quantity conservation.
The unknown command prefix makes invalidity provable. Existing socket tests
continue to cover connection failures and framing. This is deterministic fuzz
coverage, not an exhaustive fuzzer or a network throughput benchmark.

Self-tests verify histogram rounding/merging and inject a missing event,
duplicate event, and corrupted trade to prove the checker rejects them.

## Build and run

```sh
cmake -S . -B build-stress -DCMAKE_BUILD_TYPE=Release \
  -DMERIDIAN_BUILD_BENCHMARKS=OFF -DMERIDIAN_BUILD_NET=ON
cmake --build build-stress --parallel
ctest --test-dir build-stress --output-on-failure
./build-stress/meridian_stress --mode matching --cycles 100 --seed 8675309
./build-stress/meridian_stress --mode both --seconds 3600 --seed 8675309
```

Modes: `matching`, `concurrent`, or `both` (default). Choose `--cycles N`
(default 4) or `--seconds N`; duration is checked between complete cycles, so
actual runtime can exceed the requested duration by one cycle. The runner
supports durations up to 86400 seconds. Seed must be a nonzero `uint64_t`.
`--revision <7-to-40-character-lowercase-git-sha>` annotates JSON provenance.
`--self-test` runs checker/statistics/replay tests. Errors return nonzero.
Output is one JSON summary after successful completion; there is no progress
logging in the timed submit path. To reproduce a duration-run failure at cycle
C, rerun the same mode/seed with `--cycles C`.

For sanitized soaks, build a separate Release directory with
`-DMERIDIAN_ENABLE_SANITIZERS=ON` (ASan/UBSan) or
`-DMERIDIAN_ENABLE_TSAN=ON`. Sanitizers are mutually exclusive. On Linux, run:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./build-stress-asan/meridian_stress --mode both --seconds 300 --seed 8675309
```

Platform LeakSanitizer support varies; explicit Linux LeakSanitizer completion
provides the leak-check evidence. A sanitizer-free run is used for latency
reporting. Sanitized timing is diagnostic only.

## Measurement definitions

- `verified_operations_per_second`: all direct matcher submissions and concurrent
  fixture/aggressor submissions divided by whole-run wall time, including
  generation, oracle/event verification, thread lifecycle, and fixture setup.
  This measures verified workload throughput, not isolated engine peak throughput.
- `matching_latency`: two steady-clock readings around `MatchingEngine::submit`,
  including normal fill/event allocation and validation. Oracle work, event drain,
  and verification are outside that interval. Accepted and rejected requests
  contribute; rejection mix therefore affects the distribution.
- `concurrent_completion_latency`: producer-side submit-through-future-ready time
  for aggressors, including queueing, matching, snapshot publication, scheduling,
  and future overhead. Fixture submissions count toward throughput but not this
  histogram. Concurrency is closed-loop with at most four outstanding requests;
  these are not open-loop arrival/SLA or network-latency measurements.
- All latency observations are counted in fixed 4096-bin storage. Percentiles
  use nearest rank and report inclusive nanosecond bucket upper bounds. Each
  power of two has 64 subdivisions (less than 1/64 relative bucket error above
  64 ns); exact maxima are also recorded. Zero-duration clock samples map to 1 ns.
  No unbounded sample vector, downsampling, or per-operation output is used.
- Checksums exclude timings and cross-producer scheduling. Equal seeded direct
  prefixes have equal input/result checksums (events are checked separately); duration runs with different cycle
  counts naturally differ.

Record source revision, CPU/OS/compiler, build/sanitizer flags, seed, duration,
counts, and latency percentiles. Run measured Release profiles serially, outside
other builds or stress loads. Retain raw JSON. These workloads differ from the
Google Benchmark suite; their numbers are not an optimization comparison.

## CI and evidence scope

The regular build matrix runs five quick stress cases alongside all existing
checks. `soak.yml` additionally runs the full suite and a 120-second combined
soak in Release and Release ASan/UBSan on each PR and push to main. The Linux
sanitized job enables LeakSanitizer and makes its failures nonzero. JSON summaries
are uploaded as artifacts. Manual dispatch defaults to 300 seconds and permits
1–1200 seconds under a 30-minute job timeout; longer experiments use the CLI.

Recorded runs and limits are in [stress-results.md](stress-results.md). Bounded
minute-scale evidence does not replace multi-hour production soaks, open-loop
load tests, or investigations of the concurrent adapter's retained event history.
