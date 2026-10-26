# Order flows

All examples begin with an empty instance unless stated otherwise. Price is
integer ticks, quantity is units, and ID labels indicate arrival order rather
than price priority. [The runnable example](../examples/order_flow.cpp) implements
the first flow; its expected output is in the [README](../README.md).

## Better price, FIFO, and partial fill

Submit asks in order: ID 1 at 101 for 4; ID 2 at 100 for 3; ID 3 at 100 for 2.
Each initial submission emits one accepted event and rests at its price.
Drain those three events, then submit buy ID 4, limit 101, quantity 7.

| Step | Selected maker | Fill | Aggressor remaining | Book change |
| --- | --- | --- | ---: | --- |
| 1 | ID 2: oldest at best ask 100 | 3 at 100 | 4 | ID 2 removed |
| 2 | ID 3: next at 100 | 2 at 100 | 2 | ID 3 and empty 100 level removed |
| 3 | ID 1: best ask now 101 | 2 at 101 | 0 | ID 1 remains for 2 with original priority |

Maker 1 arrived first but has a worse price, so it trades last. The returned
result is accepted, remaining zero, with the three fills above. The final book
has one ask at 101, depth 2, and no bid. The seven-event batch is exactly:

```text
OrderAccepted(id=4, side=Buy, type=Limit, price=101, quantity=7)
OrderMatched(aggressor=4, resting=2, price=100, quantity=3)
TradeGenerated(aggressor=4, resting=2, price=100, quantity=3)
OrderMatched(aggressor=4, resting=3, price=100, quantity=2)
TradeGenerated(aggressor=4, resting=3, price=100, quantity=2)
OrderMatched(aggressor=4, resting=1, price=101, quantity=2)
TradeGenerated(aggressor=4, resting=1, price=101, quantity=2)
```

These labels describe typed C++ values, not the TCP wire grammar. Matching does
not also emit `OrderReducedEvent` for these internal reductions. Publish the
complete batch to market data once; matched events reduce depth and generated
trades update counters without reducing depth again.

## Limit remainder versus market remainder

With one ask ID 10 at 100 for 2, submit buy ID 11 for 5:

| Aggressor type | Fill | Returned remaining | Resulting book |
| --- | --- | ---: | --- |
| Limit at 100 | 2 at 100 | 3 | Bid ID 11 rests at 100 for 3 |
| Market (in a separate identical fixture) | 2 at 100 | 3 | Empty; no market remainder rests |

Each operation emits acceptance plus one matched/trade pair. Resting the limit
remainder does not emit a second acceptance. An empty-book market order is
accepted with no fills, its full quantity reported remaining, and one acceptance
event. A non-crossing limit rests with one acceptance event and no fills.

## Rejection before matching

Suppose ask ID 20 is resting at 100 for 2. A buy using ID 20 is rejected before
it can consume that ask. Its result is unaccepted, with empty fills and zero
remaining (the rejected input is not a resting remainder). The source emits
one `OrderRejectedEvent` with `DuplicateOrderId`; the original ask is unchanged.
IDs are checked only against currently resting orders, not a durable ledger.

Other invalid inputs produce one rejection: invalid side, unsupported type,
zero quantity, or non-positive limit price. Rejection precedence is side, type,
quantity, price, then duplicate ID. Market price is ignored by core matching.

## Direct book amendments and cancellation

This is a separate `OrderBook`, not mutation through `MatchingEngine::book()`,
which is const. Insert bid IDs 30 then 31 at 100, each for 5:

| Operation | FIFO after operation | Event |
| --- | --- | --- |
| `modify(30, 100, 3)` | 30 then 31 | Modified; priority preserved |
| `modify(30, 100, 6)` | 31 then 30 | Modified; priority lost |
| `modify(30, 101, 6)` | 31 at 100; 30 at tail of 101 | Modified; priority lost |
| `reduce_quantity(30, 2)` | ID 30 remains at 101 for 4 | Reduced; old 6, new 4 |
| `cancel(30)` | Only ID 31 at 100 | Cancelled |
| `cancel(30)` again | Unchanged | Returns false; no event |

An unchanged valid modification returns true without an event or priority change.
Missing-ID, zero-quantity, and invalid-price modifications fail without events.
A full direct reduction removes the node and emits reduced(old quantity, zero),
not a matched/trade pair. Direct edits never trigger matching: moving a bid
through an ask can create a crossed book. See [order-modification.md](order-modification.md).
