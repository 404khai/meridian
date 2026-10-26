# Order modification

`OrderBook::modify(id, price, quantity)` operates on the currently resting
quantity and applies these rules:

| Change | Priority | Event |
| --- | --- | --- |
| Quantity decrease, same price | Preserve original sequence and FIFO position | Modified with `priority_preserved=true` |
| Quantity increase, same price | Move to tail; assign new sequence | Modified with `priority_preserved=false` |
| Any price change | Move to tail at new price; assign new sequence | Modified with `priority_preserved=false` |
| Unchanged price and quantity | Preserve order exactly; return true | None |
| Missing ID, zero quantity, or non-positive price | Return false; no change | None |

The list node is spliced rather than copied on priority loss. Creating a new
price level can allocate. The emitted event contains old/new price and quantity;
normal successful modification does not also emit cancellation/acceptance.
The core does not offer transactional recovery if allocation/event emission fails.

`cancel(id)` removes a currently resting order, including a partially reduced
one, and emits a cancellation. Repeated cancellation or modification after a
full fill fails because the order no longer exists. Direct `reduce_quantity`
retains priority until quantity reaches zero, then removes the order and empty
level; it emits a reduction event.

These are direct **book operations**. `MatchingEngine::book()` is const, and
neither its submission API nor the concurrent/TCP adapters exposes modification
or cancellation. A direct price change can cross the opposite side without
executing any trades. An engine-level amend-and-rematch command is not implemented.
See the concrete [amendment flow](order-flows.md#direct-book-amendments-and-cancellation).
