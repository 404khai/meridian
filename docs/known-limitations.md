# Known limitations

These are implemented boundaries and qualification limits, not promised features.
The project establishes deterministic matching and isolated adapters; operating
an exchange service would require additional admission, recovery, and lifecycle
policies.

| Area | Current boundary | Consequence / potential next work |
| --- | --- | --- |
| Instrument scope | One book/instrument per engine; no instrument ID routing | Applications must isolate instances; no multi-asset abstraction |
| Durability | In-memory only; no journal, database, restart replay, or checkpoints | Process exit loses orders and event history |
| API surface | Engine and adapters accept submissions; only `OrderBook` directly exposes cancel/modify/reduce | No engine-level amend-and-rematch flow or TCP cancellation/modification |
| Identity | IDs unique only among resting orders; zero allowed and filled IDs reusable | Duplicate rejection is not request deduplication or exactly-once delivery |
| Direct book edits | Do not invoke matching | A direct insert/price modification can leave a locked/crossed book |
| Arithmetic | Core aggregate quantity and sequence counters have no admission/wrap policy | Callers must bound state/input; TCP has separate conservative bounds |
| Core allocation failure | Mutations/events may already be applied when an allocation throws | No transactional rollback or qualified recovery after resource exhaustion |
| Concurrent admission | Inbound queue is unbounded | Producers need an external admission/backpressure policy |
| Concurrent event history | Private log is retained until destruction; no public drain/feed API | Memory grows with lifetime traffic; finite adapter-cycle soaks do not prove bounded indefinite operation |
| Event consumption | Direct logs retain events until drained/cleared; drains are destructive | Applications must own batch boundaries and fanout; no built-in durable consumer offsets |
| Snapshot ownership | Readers may retain old shared immutable views; internal locks are possible | Memory/reclamation latency depends on readers and standard library; not a lock-free guarantee |
| Market-data feed | Requires ordered, complete batches from an empty source; no sequence/gap recovery | Cannot bootstrap midstream or detect every missing/reordered event; replay/reset is external |
| Market-data cost | Copies projection and aggregates full depth per publication | O(N log L) work and staging memory; not benchmarked as a high-frequency feed |
| Adapter wiring | TCP and concurrent adapter each own an independent engine; no automatic market-data fanout | Applications must choose/integrate an ownership path explicitly |
| TCP exposure | POSIX IPv4 loopback only, no authentication/TLS/protocol negotiation | Not a remotely exposed production gateway |
| TCP delivery | Complete orders can execute before a response is lost; shutdown need not flush replies | Disconnect is an uncertain result; no safe blind retry or rollback |
| TCP admission | 64 clients, 256-byte lines, 64 KiB unsent output/client, 100000 resting orders, quantity up to 1e12 | Excess clients/oversized buffers close; full book rejects all new limits even if crossing |
| TCP idle policy | Nonblocking I/O, no idle timeout | A stalled client does not block socket I/O but can occupy a slot |
| Executables/packaging | `meridian_engine` is a smoke check; no file-replay CLI or installed CMake package | Use the example/TCP executable or embed source targets |
| Platforms | CI covers Linux GCC and macOS Apple Clang; TCP is POSIX-only | Windows/MSVC behavior is not qualified by the current matrix |
| Performance evidence | Specific synthetic workloads on shared hosts, one storage design, no open-loop overload characterization | Results are observations, not network/end-to-end SLA or alternate-container superiority claims |
| Stress qualification | Recorded minute-scale soaks, not multi-hour/adversarial production traffic | Longer runs, measured RSS growth, recovery drills, and broader load models remain future qualification |

The concurrent adapter forwards exceptions to futures and continues its loop.
That mechanism does not make core allocation failure recoverable or guarantee
that a failed future denotes an unapplied order. Likewise, network delivery
failure never undoes successful matching. Neither API supplies durable outcome
reconciliation.

No self-trade prevention, trading sessions, order expiry, hidden/iceberg orders,
stop orders, risk/margin checks, instrument tick/lot validation, market-data wire
protocol, or GUI is implemented. Limit/market price-time semantics and direct
book modification are the defined domain scope. Extending it should preserve
[the project's priority order](../AGENTS.md), add tests, document decisions, and
include evidence for any performance-motivated change.
