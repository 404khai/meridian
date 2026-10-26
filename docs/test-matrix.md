# Test matrix

CTest entries group multiple assertions and scenarios; counts are not line or
branch coverage percentages. With both optional layers enabled there are 16
entries, 13 without networking, 15 without market data, and 12 without both.
All production-code changes require relevant tests in the same PR.

| CTest entry | Coverage | Availability |
| --- | --- | --- |
| `meridian_tests` | Book insertion/cancel/query, FIFO, invalid orders, empty state | Always with testing |
| `meridian_matching_tests` | Price/FIFO priority, partial/market fills, invalid/duplicate submissions, two-sided large-sweep event values after node removal | Always |
| `meridian_modification_tests` | Quantity/price priority loss, no-op/invalid amendments, cancellation/reduction edge cases | Always |
| `meridian_event_tests` | Typed acceptance/rejection/cancel/modify/reduce/match/trade fields and ordering | Always |
| `meridian_comprehensive_tests` | Fixed edge matrix, seeded 1500-order replay/conservation, no-crossed invariant, 10000-order book and 2000-maker sweep | Always |
| `meridian_queue_tests` | Multi-producer queue, 20000 items, close/drain semantics | Always |
| `meridian_concurrency_tests` | Eight producers/four snapshot readers, simultaneous aggressors, duplicate IDs, queued shutdown | Always |
| `meridian_marketdata_tests` | 1200-submission event projection/replay against source, direct edits, counters, rollback/overflow, retained snapshots and readers | Market data enabled |
| `meridian_protocol_tests` | Command parsing, numeric bounds, exact wire fills/replay, admission/response limits | Networking enabled |
| `meridian_tcp_tests` | Real loopback framing, pipelining, shared state, half-close/reset, capacity, oversized I/O, stop/start failures | Networking enabled |
| `meridian_stress_self_tests` | Histogram rounding/merge, seeded replay, injected dropped/duplicate event and corrupt trade | Always |
| `meridian_stress_matching` | Independent vector oracle, exact events, depth/FIFO and book invariants, seed 42 | Always |
| `meridian_stress_adversarial` | Same matching oracle with seed 8675309 and invalid/extreme sequences | Always |
| `meridian_stress_concurrent` | Four producers, each maker/future once, snapshots and repeated teardown | Always |
| `meridian_protocol_adversarial_tests` | 20000 malformed byte/control/oversized requests interleaved with valid fills, recovery and conservation | Networking enabled |
| `meridian_layering_check` | Core direct includes cannot point into market data or networking | Always |

The five stress entries are labeled `stress`; the networking-disabled build has
four. Fixed-cycle generators have nonzero seeds and bounded state, so failures
are repeatable rather than time-dependent randomized assertions. The matching
oracle uses a different vector/linear-search design from production containers.

## CI configurations and long runs

The [build matrix](../.github/workflows/build.yml) runs Release, Debug,
ASan/UBSan, TSan, networking-disabled, and market-data-disabled configurations.
TSan uses macOS; the other jobs use Linux. New warnings fail the build. The
normal suite includes the layer check even when optional sources are excluded.

The separate [soak workflow](../.github/workflows/soak.yml) runs the full suite
and a duration workload in Release and Linux ASan/UBSan/LeakSanitizer. Raw JSON
artifacts record revision, seed, counts, throughput, and latency bounds.

[Stress methodology](stress-testing.md) explains per-operation oracle/event
checks, concurrent maker accounting, bounded memory, and latency quantization.
[Recorded evidence](stress-results.md) distinguishes minute-scale/no-detected-leak
runs from long-lived concurrent history growth and multi-hour qualification.
[Contributor commands](contributing.md) reproduce each build configuration.
