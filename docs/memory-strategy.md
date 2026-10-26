# Memory strategy

Meridian uses standard-library allocation and explicit ownership. There is no
custom allocator, slab, order pool, memory mapping, or lock-free reclamation.
The purpose is predictable object lifetimes and measurable behavior, not a
claim of allocation-free execution or a fixed memory budget for the core.

## Core ownership and allocations

| Object | Owner and lifetime | Allocation/retention behavior |
| --- | --- | --- |
| Resting order | FIFO `std::list<Order>` node in its price level | Allocates on insertion; erased on cancel/full fill |
| Price level | Bid/ask `std::map` node | Allocates for new price; erased when last order leaves |
| Order-ID location | `std::unordered_map` entry | Stores non-owning list iterator; growth may rehash; bucket capacity can outlive orders |
| `SubmitResult::fills` | Returned value owned by caller | Vector growth during matches; released/reused by caller |
| `EventLog` | Owned by `OrderBook` | Vector grows until drained/cleared or source destruction |
| Query values | Caller | `find` copies one order; `orders_at` allocates/copies the level |

Book/engine owners do not export mutable node pointers. An ID-index iterator
points into the corresponding FIFO list; cancellation removes that index entry
and list node together. Partial fills change quantity in place. Priority-losing
modification splices the existing list node to the destination tail, preserving
node identity while updating price and sequence. A new destination map level
may allocate first. Book copying/moving is disabled to protect internal locations.

Matching copies just the best opposing head. It does not allocate a temporary
full-level vector per fill, but normal result/event vectors and book insertion
still allocate. All emitted events own their fields by value; they do not refer
to erased makers.

`drain_events()` transfers the vector to the caller and leaves an empty source
log. The drained vector remains allocated until its consumer releases it.
`EventLog::clear()` removes entries but may retain capacity. Consumers requiring
multiple views should fan out one complete drained batch, rather than drain the
same source twice. Inspection references borrow the source and must not outlive its mutations or
drain; use owned batches for retained event data. No implicit event-history bound
or background consumer exists.

## Worker and reader lifetimes

The concurrent adapter holds shared submissions during queue handoff, promises,
and one worker-owned engine. Producers receive futures; the unbounded queue
has no admission/backpressure policy. Its private engine event log has no public
drain path, so history grows with accepted/rejected inputs and fills until
adapter destruction. A successful finite soak with adapter recreation is not
proof of bounded memory for an indefinitely running adapter.

Both snapshot publishers store `shared_ptr<const ...>` values atomically with
acquire/release semantics. Readers can retain old versions after a new version
is published, which justifies shared ownership. Memory is reclaimed only when
all owners of a version release it. This may involve internal library locks;
it does not provide lock-free or real-time reclamation guarantees.

Market-data publication stages a complete copy of its order projection, builds
price aggregates, allocates a snapshot, then swaps/commits. Memory includes old
projection, staged projection, aggregation maps, new snapshot, and reader-retained
versions. Work is O(N log L), with O(N) projection copying, for N orders/L levels.
Detected invalid batches, checked overflow, or allocation failure before commit
preserve that publisher's previous state. This rollback contract is specific to
the projection and does not make matching transactional.

## Transport and measurement bounds

TCP caps admitted clients at 64, buffered input at 256 bytes per client and
unsent output at 65536 bytes per client. Its session caps resting orders at
100000 and quantity at 1e12. Those logical limits do not include allocator
capacity overhead, kernel socket buffers, fill/event vectors, or temporary
serialization records. A reply can exceed the budget after an order executes;
the connection then closes without rollback. See [the protocol](network-protocol.md).

The stress driver keeps one direct engine, at most 256 resting orders, drained
event history, and fixed 4096-bin histograms. Concurrent adapters are torn down
per bounded cycle. The oracle/workload storage does not grow with elapsed time;
allocator/sanitizer caches can still affect resident memory. Raw runs report
correctness, timing, and sanitizer evidence, not measured RSS ceilings.

## Failure and arithmetic limits

The core does not enforce aggregate quantity overflow or sequence-counter
admission bounds. Application-owned direct/worker paths must keep inputs and
retained state within representable limits; strong wrappers alone do not provide
that policy. The TCP adapter bounds admission separately. Market data checks
selected quantity/counter sums and rejects overflow at publication.

Core insertion, matching, modification, and event emission do not provide a
transactional allocation-failure guarantee. An exception can follow partial
mutation; continuing after allocation failure is not qualified as safe recovery.
The worker forwards exceptions through futures, which does not add rollback.
No crash recovery or persistence mechanism exists. See
[known-limitations.md](known-limitations.md) for integration consequences.
