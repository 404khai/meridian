# Event contract

Each `OrderBook` owns a single-threaded `EventLog`, a vector of `std::variant`
value events. `MatchingEngine` exposes that same log through its book.
`entries()`/`at()` inspect it; `drain_events()` transfers its current batch and
leaves the source empty. Neither core event creation nor inspection performs
I/O, callbacks, or synchronization.

| Operation | Emission on successful/valid operation |
| --- | --- |
| Direct book insert | One `OrderAcceptedEvent` |
| Engine submission | Acceptance, then one `OrderMatchedEvent` and `TradeGeneratedEvent` per fill |
| Cancel existing order | One `OrderCancelledEvent` |
| Changed valid modification | One `OrderModifiedEvent` with old/new fields and priority-preserved flag |
| Direct quantity reduction | One `OrderReducedEvent` with old/new quantities; new zero means removal |
| Invalid book insert / engine submission | One `OrderRejectedEvent` with ID and reason |

A successful engine submission emits acceptance even when a market order finds
no liquidity. A partially filled limit rests its remainder without a second
acceptance. Engine-internal fills suppress direct reduction events, so the
matched/trade pair does not also duplicate a low-level reduction event.

Missing-ID cancellation, invalid modification/reduction, and unchanged valid
modification produce no events. A no-op modification returns true; invalid
operations return false. Full fills remove orders without a cancellation event.
An application interpreting an operation batch must know whether it is observing
a direct book operation or an engine match.

Accepted fields describe the original input, not just the resting remainder.
Matched and trade fields identify aggressor, maker, maker price, and executed
quantity. Events contain values, not references to potentially erased nodes.
Rejection precedence for engine submission is invalid side, unsupported type,
zero quantity, non-positive limit price, then duplicate currently resting ID.

One complete operation batch is the market-data consumption boundary. Applications
must drain/fan out once and preserve order; events contain no global sequence,
operation-complete marker, durable offset, or restart metadata. The concurrent
adapter retains its private log with no drain API; TCP drains/discards its own
session's events. See [order flows](order-flows.md), [market-data feed rules](market-data.md),
and [memory strategy](memory-strategy.md) for examples and retention costs.
