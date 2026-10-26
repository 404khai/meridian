# Concurrency model

## Ownership and data flow

`ConcurrentMatchingEngine` is a concurrency adapter around the existing
single-threaded `MatchingEngine`. It does not make the book itself concurrent.

1. Any producer thread may call `submit(Order)`.
2. The submission and its completion promise enter a mutex-protected FIFO
   `BlockingQueue`.
3. One dedicated worker thread removes requests and is the only thread that
   owns or calls `MatchingEngine`.
4. After each processed request, the worker atomically publishes an immutable
   `BookSnapshot` for readers.
5. The worker then makes the producer's `std::future` ready with the result.

This preserves all proven single-threaded matching semantics. The concrete
queue order is the deterministic input sequence. Calls made by one producer
retain program order; ordering between producers is determined by queue-lock
acquisition and is intentionally not promised.

## Producer/consumer queue

`BlockingQueue<T>` uses one mutex and condition variable. `push()` is safe for
multiple producers. The worker blocks in `wait_pop()` without polling. Closing
the queue rejects later pushes, wakes an empty consumer, and still permits all
already-enqueued requests to drain.

The queue is unbounded. Producers are responsible for controlling
admission volume; bounded backpressure policy requires workload evidence and
is not implemented by this adapter.

The queue is intentionally lock-based. Lock-free queues and custom allocators
remain unimplemented; changing that requires benchmark evidence.

## Single writer and multiple readers

The worker is the sole mutable-state writer. No mutable `OrderBook` or
`MatchingEngine` reference escapes the adapter.

Readers call `snapshot()` and atomically load a
`std::shared_ptr<const BookSnapshot>` with acquire semantics. The worker stores
each fully constructed snapshot with release semantics. Snapshots contain best
bid/ask, order and level counts, and a monotonically increasing processed
submission count. Their immutability allows any number of readers to retain and
inspect a view without locking or racing with later publications.

This is a concurrency-state view, separate from full-depth market data. It
does not publish depth, trades, or external protocol data.

## Shutdown

Destruction closes the inbound queue and joins the worker. The worker processes
all requests accepted before close, so their futures become ready before the
adapter is destroyed. As with ordinary C++ objects, callers must not invoke
methods concurrently with destruction.

## Testing and sanitizer contract

The original concurrency suites cover:

- 20,000 items through four queue producers and one consumer;
- 4,000 non-crossing orders from eight producers while four readers inspect
  snapshots;
- 2,000 concurrent aggressors consuming 2,000 resting orders exactly once;
- concurrent duplicate-ID rejection; and
- shutdown with 1,000 queued submissions.

The dedicated TSan configuration is:

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DMERIDIAN_ENABLE_TSAN=ON -DMERIDIAN_BUILD_BENCHMARKS=OFF
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure
```

ASan/UBSan and TSan are mutually exclusive build modes. CMake rejects enabling
both in one build directory.

## Market-data publication

`MarketDataPublisher` separately supports one event-consumer writer and multiple
immutable-snapshot readers. It receives complete operation batches from an
application-owned source and publishes after projection and aggregation succeed.
The existing concurrent ingestion adapter still exposes its original lightweight
`BookSnapshot`; its worker does not expose an event feed. Market-data wiring
belongs outside the core. See [market-data.md](market-data.md) for ownership,
feed boundaries, memory costs, and reader lifetime requirements.

## TCP ownership boundary

The optional TCP server has its own single-owner matching session. One
nonblocking poll loop processes clients serially; it does not submit to the
existing producer/consumer adapter. Per-client commands retain FIFO order;
cross-client execution order follows event-loop readiness/traversal. No mutable
engine reference escapes, and socket I/O never blocks the owner. Worker-using
applications must request stop and join before server destruction. See
[network-protocol.md](network-protocol.md) for bounded buffers, uncertain
delivery, and adapter lifecycle.

The private event log is retained until adapter destruction; there is no public
event-drain API. Both the queue and retained history can grow with lifetime
traffic. [Memory strategy](memory-strategy.md) and [known limitations](known-limitations.md)
distinguish finite lifecycle stress evidence from bounded long-lived operation.
