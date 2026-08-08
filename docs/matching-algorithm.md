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

The returned `Fill` sequence is deterministic for a deterministic submission
sequence. Trade/event queues are intentionally deferred to Phase 4.
