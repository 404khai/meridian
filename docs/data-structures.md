# Book data structures and complexity

The book is single-threaded and owns all resting state. For N resting orders,
L price levels on the relevant side, and K orders at a selected price:

| Structure | Purpose | Choice and tradeoff |
| --- | --- | --- |
| `std::map<Price, PriceLevel>` per side | Ordered price levels | Simple logarithmic insert/find; node allocations and pointer traversal |
| `std::list<Order>` per level | FIFO time priority | Stable iterators, constant-time removal/splice once located; allocation per order |
| `std::unordered_map<OrderId, Location>` | ID lookup | Average constant time; possible rehashes and O(N) worst-case lookup |
| `PriceLevel::quantity` | Aggregated depth | Constant-time update/query after level lookup; unsigned sum requires input bounds |
| `EventLog` vector | Typed ordered events | Contiguous value records; allocation/growth and retention until consumed |

Bids use the map's reverse end for highest-price-first matching; asks use begin
for lowest-price-first. Each ID location stores side, price, and a non-owning
iterator into its level list. It does not cache a map-level iterator.

## Operation costs

These describe container work and exclude vector growth/allocation pauses and
output copying unless stated. Hash costs are average-case, not worst-case bounds.

| Operation | Typical complexity | Detail |
| --- | --- | --- |
| `contains` / `find` | O(1) average | Hash lookup; `find` returns an order value |
| Best bid/ask | O(1) | Empty check plus map endpoint |
| Order/level counts | O(1) | Stored container sizes |
| `insert` | O(log L) amortized/average | ID validation, map lookup/create, FIFO append, ID insertion |
| `cancel` / `reduce_quantity` | O(log L) average | Hash ID lookup then **map find by stored price**, list erase/update, possible level removal |
| `modify` | O(log L) average | Old/new map lookup and optional level allocation; priority loss splices to tail |
| `depth_at` | O(log L) | Map lookup then stored quantity |
| `orders_at` | O(log L + K) | Map lookup plus allocated copy of the whole FIFO |
| Sweep with F fills | O(F log L) average, plus possible remainder insertion | One head copy and indexed reduction per fill; level count changes during sweep |

When there is one price level, sweep work is linear in the number of fills.
The prior implementation copied the remaining FIFO for every fill, which made
that case quadratic. The current matcher privately obtains one best-head value,
which survives node removal during event emission. Public `orders_at()` retains
its full-copy semantics. See [benchmark evidence](benchmark-results.md).

`OrderBook` stores only positive-price, positive-quantity limits; it never matches
on insert or modify. `MatchingEngine` adds limit/market crossing semantics.
Partial fills retain queue position and sequence. Quantity-decrease amendments
keep priority; quantity increases or price changes move to the destination tail.
Empty levels are removed. No internal iterator is exposed publicly.

See [memory strategy](memory-strategy.md) for allocation/lifetime behavior and
[known limitations](known-limitations.md) for unchecked core arithmetic and
allocation-failure recovery boundaries.
