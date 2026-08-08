# Phase 3 order modification

`OrderBook::modify(order_id, price, quantity)` applies these priority rules:

- A quantity decrease at the same price keeps the order's original sequence
  and FIFO position.
- A quantity increase at the same price loses priority and moves the order to
  the back of its price level.
- Any price change loses priority and moves the order to the back of the new
  price level, regardless of whether quantity increased or decreased.
- An unchanged price and quantity is accepted as a no-op and preserves the
  order exactly.
- Missing order IDs, zero quantities, and non-positive prices are rejected
  without changing the book.

Modification is a book operation only in Phase 3. It does not generate events
or trigger new matching; event generation is Phase 4 scope.
