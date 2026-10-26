# Stress and soak results

Recorded on 2026-10-26. Harness source: `61aa88656d9d19fd1481c8d4c67fdf5f02ca213c`.
Production matching, concurrency, and transport sources are unchanged.

## Local Release profiles

Apple M3 (8 cores), macOS 27.0 (26A428), arm64; Apple Clang 21.0.0
(clang-2100.3.34.2), CMake Release, sanitizers disabled. Seed 8675309.
The two profiles ran serially without other builds/test runs on this shared
development host. CPU affinity and background scheduling were not controlled.

| Workload | Duration | Verified submissions | Verified events | Verified operations/s |
| --- | ---: | ---: | ---: | ---: |
| Direct matching | 60.0010 s | 114,758,820 | 304,748,460 | 1,912,620 |
| Concurrent ingestion | 60.0006 s | 22,039,552 | 0 | 367,322 |

Concurrent event count zero means events are not externally observable through
that adapter. Its run verifies 11,019,776 fills/completions and final snapshots;
it does not assert that zero events were generated.

| Measured latency | Samples | p50 ns | p95 ns | p99 ns | p99.9 ns | Exact max ns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Matching submit (including rejections) | 114,758,820 | 84 | 335 | 459 | 2,687 | 14,884,750 |
| Concurrent aggressor completion | 11,019,776 | 5,567 | 13,567 | 24,575 | 43,007 | 25,089,417 |

Percentiles are nearest-rank inclusive histogram bucket upper bounds, not exact
individual samples (less than 1/64 relative bucket error above 64 ns). Maxima
are exact observed durations. Matching measurements surround `submit`, including
normal validation/fill/event allocation, and exclude the oracle and event drain.
Concurrent measurements include submit, queueing, worker/snapshot work, scheduling,
and future readiness; only aggressors are sampled. Each of four producers has at
most one outstanding request. Tail maxima include development-host scheduling.

Throughput includes setup, generation, verification, and lifecycle cost. It is
verified workload throughput, not isolated engine peak capacity, open-loop SLA
evidence, network latency, or a before/after optimization comparison. About
14.7% of the direct matching submissions are intentionally rejected; changing
the workload mix changes its latency distribution.

## Sanitized soak and leak checks

| Run | Duration | Matching submissions | Checked events | Concurrent submissions | Concurrent fills |
| --- | ---: | ---: | ---: | ---: | ---: |
| Local Release ASan/UBSan | 300.006 s | 63,288,324 | 168,066,080 | 14,877,696 | 7,438,848 |
| Linux Release | 120.008 s | 31,763,952 | 84,351,384 | 7,467,008 | 3,733,504 |
| Linux Release ASan/UBSan/LSan | 120.018 s | 9,648,540 | 25,621,904 | 2,268,160 | 1,134,080 |

All runs completed with successful exit status and no accounting mismatch.
The local 300-second ASan/UBSan run captured empty sanitizer stderr. Optional
build validation ran during that soak; its timing fields are diagnostic only.
Local Apple-runtime success is not used as explicit LeakSanitizer evidence.

The [Linux CI run](https://github.com/404khai/meridian/actions/runs/40400585812)
passed the full suite and both 120-second combined soaks. The sanitized job used
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`,
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, and `LSAN_OPTIONS=exitcode=23`.
No ASan, UBSan, or LeakSanitizer report occurred; both jobs completed successfully.
This is no-detected-leak evidence at process teardown over the recorded interval.
The host was GitHub `ubuntu-latest` with GCC 15.2.0; hardware was not characterized,
so its timing fields are not compared with the local M3 profiles.

CI artifacts annotate virtual merge revision
`3fff1f4d82d6b275cb3f2f446412e3517d8d3f87`, whose parents are baseline
`83f6981` and harness revision `61aa886`. The recorded workload code is therefore
the same as the local source revision.

## Interpretation and validation limits

One matching engine remains alive across all direct cycles; its book repeatedly
fills and drains, and consumed event history is released. Concurrent adapters
are recreated per cycle to exercise teardown and bound their existing retained
event histories. This does not establish bounded memory for an indefinitely
lived concurrent adapter, which has no public event-drain API.

The independent oracle checks every direct result and every event field and
position, not just the reported totals. Fault-injection self-tests detect a
dropped event, duplicated event, and corrupted trade. Concurrent checks cover
maker uniqueness, quantities, futures, and processed-submission snapshots. No
missing/duplicate/corrupted direct events or concurrent fills were detected.
Minute-scale observations do not constitute multi-hour or production-load
qualification; the configurable runner supports further experiments.

The normal matrix passes 16/16 tests in Debug, Release, Debug ASan/UBSan, Debug
TSan, and optimized ASan/UBSan. Networking-disabled and market-data-disabled
builds pass 13/13 and 15/15. The new binary-frame generator validates 20000
malformed requests interleaved with matching and quantity conservation.

## Reproduction and raw summaries

See [stress-testing.md](stress-testing.md) for commands, exact workload definitions,
latency boundaries, replay, and histogram quantization. The `accepted`, `rejected`,
`trades`, and `verified_events` JSON totals describe direct matching only;
concurrent counters have explicit prefixes. Checksums summarize direct result
fields and IDs; exact event validation is performed separately.

- [Local environment, commands, and exit evidence](stress-results/2026-10-26-environment.json)
- [Release matching](stress-results/2026-10-26-release-matching.json)
- [Release concurrent ingestion](stress-results/2026-10-26-release-concurrent.json)
- [Local 300-second ASan/UBSan soak](stress-results/2026-10-26-local-asan-soak.json)
- [Linux Release soak](stress-results/2026-10-26-linux-release-soak.json)
- [Linux ASan/UBSan/LeakSanitizer soak](stress-results/2026-10-26-linux-asan-soak.json)
- [Linux CI provenance and leak-check settings](stress-results/2026-10-26-linux-ci.json)
