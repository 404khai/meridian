# Event-driven market data

`meridian_marketdata` is an optional library that depends on core event types.
The core engine never includes or links market data. There is no networking,
I/O, callback dispatch, or logging in the publisher.

## Feed and publication contract

Start `MarketDataPublisher` alongside an empty `OrderBook` or `MatchingEngine`.
One writer must deliver all events, once, in order, as **complete operation
batches**. For example, drain events after each `submit`, `insert`, `cancel`,
`modify`, or `reduce_quantity` call. Empty batches can represent operations
that emitted no events, including a failed cancellation or unchanged modify.
A publication increments the operation count even for an empty batch.

Accepted limit events initially add the entire incoming quantity. Matched
events subtract executed quantity from the resting maker and from a limit
aggressor. Accepted market orders never enter resting depth; their unfilled
quantity is discarded. Trade events update trade counters without subtracting
depth again. Rejections leave depth unchanged. Modification, explicit quantity
reduction, and cancellation events update the tracked order by ID.

Publish only after the entire source operation. Acceptance followed by matching
can transiently represent a crossed book inside the projection; the staged
intermediate state is never exposed. A batch may contain multiple complete
operations, but it then contributes one operation/latency sample; use one source
operation per publication for source-operation throughput.

The publisher maintains its own order projection and rebuilds aggregated depth
at publication. It stages all changes and allocations before swapping state
and publishing, so detected invalid batches, quantity/counter overflow, and
allocation failures leave the previous projection and snapshot intact. It checks
unknown cancellation/reduction IDs, duplicate accepted limits, and inconsistent
old quantities/prices. It is **not** a protocol validator: existing events have
no source sequence numbers, end-of-operation markers, or recovery metadata.
Missing/reordered batches and incomplete trade pairs cannot all be detected.
Starting midstream or recovering after a feed gap requires a new publisher and
replay from an empty source. Unrelated event consumers must fan out batches;
each `drain_events()` removes the source log.

## Snapshot contents

`MarketDataSnapshot` contains:

- A publication sequence starting at zero and increasing once per successful batch.
- Optional best bid/ask and signed spread in price ticks. Spread is absent if
  either side is empty; zero or negative spread is possible for a directly
  manipulated, locked/crossed `OrderBook`.
- Full aggregated bid depth descending by price and ask depth ascending by
  price, including quantity and resting-order count per level.
- Total resting-order count and cumulative observability counters.

`MarketDataCounters` reports operations, events, accepted/rejected orders,
trades, traded quantity, observation elapsed time, and minimum/maximum/total
operation latency. `operations_per_second()` is cumulative successful
publications divided by elapsed seconds; it returns zero when elapsed time is
zero. `mean_latency_ns()` divides total latency by operations, returning zero
before the first operation. A rejected order is a completed observed operation.
A rejected publication does not change counters.

Latency is supplied by the caller, so it can measure processing time or
end-to-end time including queueing. Choose and document one interval per feed.
The publisher does not instrument matching or conflate publication cost with
source processing latency. Observation uses `steady_clock`, starts at publisher
construction, and must not go backwards. Constructor/publish timestamps can be
injected for deterministic tests. Timing counters are observational; book
projection and event ordering do not depend on wall-clock time.

## Ownership and cost

One writer calls `publish`. Any number of readers call `snapshot`, atomically
loading a `shared_ptr<const MarketDataSnapshot>` with acquire semantics. The
writer publishes with release semantics. Readers can retain old snapshots;
shared ownership is required for their independent lifetimes. Shared-pointer
atomic operations may use internal locks; no lock-free guarantee is made.
Calls must not race with destruction.

Projection staging is O(N); full depth aggregation is O(N log L), where N is
resting-order count and L is price levels. Each publication allocates a new
immutable snapshot and stages the projection. Retained snapshots consume memory
until released by readers. This correctness-first interface makes no new
performance claim. It leaves the concurrent ingestion adapter and its lightweight
`BookSnapshot` unchanged: that adapter currently does not expose a lossless event
feed. An application owning a matching worker can drain and fan out its events
into this publisher on that same worker. Do not read the mutable source book
from snapshot-reader threads.

## Example

```cpp
#include <chrono>
#include "meridian/engine/matching_engine.hpp"
#include "meridian/marketdata/market_data_publisher.hpp"

int main() {
    meridian::MatchingEngine engine;
    meridian::MarketDataPublisher market_data;
    const auto started = meridian::MarketDataPublisher::Clock::now();
    const auto result = engine.submit(meridian::Order{
        meridian::OrderId{1}, meridian::Side::Buy, meridian::OrderType::Limit,
        meridian::Price{100}, meridian::Qty{5}});
    const auto finished = meridian::MarketDataPublisher::Clock::now();
    const auto events = engine.drain_events();
    market_data.publish(events,
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started));
    const auto view = market_data.snapshot();
    return result.accepted && view->best_bid == meridian::Price{100} &&
        view->bids.front().quantity == meridian::Qty{5} ? 0 : 1;
}
```

Link the application with `meridian_marketdata`. Build without the layer using
`-DMERIDIAN_BUILD_MARKETDATA=OFF`; core and concurrency tests still run. The
CTest dependency-direction check runs in both configurations and CI rejects
core includes of market-data or networking headers.
