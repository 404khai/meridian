# Design decisions

## DD-001: Google Benchmark for Phase 6

Phase 6 adds Google Benchmark as a build-time-only dependency, pinned to the
`v1.9.5` commit `192ef10025eb2c4cdd392bc502f0c852196baa48` through CMake
`FetchContent`. The project specification explicitly requires Google Benchmark
for the `meridian_benchmarks` target. It provides calibrated iterations,
timing controls for excluding fixture setup, compiler-optimization barriers,
machine-readable output, and comparable throughput counters without adding
any dependency to `meridian_core` or production executables. Its tests and
installation rules are disabled. Benchmark builds can be omitted with
`-DMERIDIAN_BUILD_BENCHMARKS=OFF` for offline or production-only builds.

## DD-002: Single-writer concurrent ingestion

Phase 7 retains the existing `MatchingEngine` as a single-threaded component
and adds a separate `meridian_concurrency` adapter. Producers communicate with
one owner thread through a mutex/condition-variable queue and receive results
through futures. Readers consume atomically published immutable snapshots.
This makes ownership explicit, preserves the determinism of each concrete
input sequence, and avoids contaminating the core book with locks. A
lock-based queue is the deliberate correctness-first baseline; lock-free
structures remain gated on Phase 8 benchmark evidence.

## DD-003: Remove repeated level snapshots from matching

The recorded benchmark baseline identifies repeated `orders_at()` snapshots as
the cause of quadratic single-level sweeps. The matcher now copies just the
best FIFO head through a private book helper. A value snapshot is necessary:
full fills erase the maker node before match/trade events are emitted. This
removes the temporary vector allocation and repeated queue traversal without
changing public queries, storage ownership, or the concurrent adapter. No new
dependency is introduced. Pools, custom allocators, and lock-free queues remain
deferred until measurements identify a separate need for them. See the paired
measurements in `benchmark-results.md`.

## DD-004: Batch-based event projection for market data

The optional `meridian_marketdata` target consumes existing events and owns a
separate resting-order projection. Complete operation batches prevent readers
from observing a temporarily crossed projection between limit acceptance and
its fills. `OrderMatchedEvent` changes depth; `TradeGeneratedEvent` changes only
trade counters. Immutable snapshots use atomic shared-pointer publication for
reader lifetimes, following the proven concurrent-ingestion ownership pattern.
Staging the projection gives failed publications rollback semantics at the cost
of O(N) copying and full depth aggregation per publication. This is a new
interface, not a performance optimization. Source timing is caller-supplied so
core remains free of market-data, I/O, and clock instrumentation. No dependency
is added. The existing concurrent adapter keeps its independent lightweight
snapshot API; this layer consumes batches from an application-owned source.
See `market-data.md` for event-feed requirements and observability definitions.
