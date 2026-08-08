# Phase 1 data structures

The Phase 1 book is single-threaded and owns all order state.

- Bid and ask sides use `std::map<Price, PriceLevel>`. Bids are queried from
  the reverse iterator for highest-price-first behavior; asks use the normal
  begin iterator for lowest-price-first behavior.
- Each `PriceLevel` stores orders in a `std::list<Order>`, so appending an
  order preserves FIFO time priority and cancellation does not invalidate
  iterators to other orders at the level.
- An `std::unordered_map<OrderId, Location>` provides O(1)-average lookup and
  cancellation. `Location` stores the side, price, and list iterator.

Phase 1 accepts positive-quantity limit orders. Market orders are rejected
until the matching engine exists in Phase 2.
