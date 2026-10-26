# Phase 2 matching algorithm

`MatchingEngine::submit` treats each submitted order as the aggressor and
matches it against the opposite side of the `OrderBook`.

1. Select the best opposing price: lowest ask for a buy, highest bid for a
   sell.
2. Stop when a limit order no longer crosses the best opposing price. Market
   orders continue until their quantity is filled or the opposing side is
   empty.
3. Consume the first order at the selected level. Orders at one price level
   are already stored FIFO by Phase 1, so this gives deterministic price-time
   priority.
4. Reduce the resting order quantity. A partial fill retains its list
   position; a full fill removes the order.
5. Rest any unfilled limit-order remainder at its original price. Market-order
   remainders are discarded because they cannot rest in the book.

The matcher privately copies only the FIFO head of the best opposing level.
The value copy survives full-fill removal, so subsequent match and trade events
can safely use its ID and price. No list iterator or reference escapes to the
matcher. Selecting a head takes constant time and no allocation; a sweep of N
orders at one price copies N orders instead of N(N+1)/2. Book reduction still
performs its existing indexed lookup and price-level map lookup. Fill and event
vectors retain their normal allocations.

The returned `Fill` sequence and accepted/matched/trade event sequence are
deterministic for a deterministic submission sequence.
