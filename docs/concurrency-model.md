# Phase 7 concurrency model

## Ownership and data flow

`ConcurrentMatchingEngine` is a concurrency adapter around the existing
single-threaded `MatchingEngine`. It does not make the book itself concurrent.

1. Any producer thread may call `submit(Order)`.
2. The submission and its completion promise enter a mutex-protected FIFO
   `BlockingQueue`.
3. One dedicated worker thread removes requests and is the only thread that
   owns or calls `MatchingEngine`.
4. The worker publishes the result through the producer's `std::future`.
5. After each processed request, the worker atomically publishes an immutable
   `BookSnapshot` for readers.

This preserves all proven single-threaded matching semantics. The concrete
queue order is the deterministic input sequence. Calls made by one producer
retain program order; ordering between producers is determined by queue-lock
acquisition and is intentionally not promised.

## Producer/consumer queue

`BlockingQueue<T>` uses one mutex and condition variable. `push()` is safe for
multiple producers. The worker blocks in `wait_pop()` without polling. Closing
the queue rejects later pushes, wakes an empty consumer, and still permits all
already-enqueued requests to drain.

The Phase 7 queue is unbounded. Producers are responsible for controlling
admission volume; bounded backpressure policy requires workload evidence and
is not chosen implicitly in this phase.

The queue is intentionally lock-based. Phase 7 proves ownership and race-free
semantics first; lock-free queues and allocator changes are Phase 8 work and
require evidence against the Phase 6 benchmark baseline.

## Single writer and multiple readers

The worker is the sole mutable-state writer. No mutable `OrderBook` or
`MatchingEngine` reference escapes the adapter.

Readers call `snapshot()` and atomically load a
`std::shared_ptr<const BookSnapshot>` with acquire semantics. The worker stores
each fully constructed snapshot with release semantics. Snapshots contain best
bid/ask, order and level counts, and a monotonically increasing processed
submission count. Their immutability allows any number of readers to retain and
inspect a view without locking or racing with later publications.

This is a concurrency-state view, not the Phase 9 market-data interface. It
does not publish depth, trades, or external protocol data.

## Shutdown

Destruction closes the inbound queue and joins the worker. The worker processes
all requests accepted before close, so their futures become ready before the
adapter is destroyed. As with ordinary C++ objects, callers must not invoke
methods concurrently with destruction.

## Testing and sanitizer contract

Phase 7 tests cover:

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
