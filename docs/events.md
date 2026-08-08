# Phase 4 events

Meridian uses a typed `std::variant` event stream stored in `EventLog`. The
log is single-threaded and owned by each `OrderBook`; `MatchingEngine` exposes
the same log through its book.

The stream includes:

- `OrderAcceptedEvent` and `OrderRejectedEvent` for submissions;
- `OrderCancelledEvent` for successful cancellation;
- `OrderModifiedEvent` for successful modifications, including whether
  priority was preserved;
- `OrderReducedEvent` for direct quantity reductions/fills;
- `OrderMatchedEvent` for each aggressor/resting-order match; and
- `TradeGeneratedEvent` containing the generated `Trade` record.

Matching uses the higher-level matched/trade pair for internal reductions so
one fill does not also produce a duplicate low-level reduction event. Events
are queryable through `entries()`/`at()` and consumable through `drain()`.
No synchronization or I/O is part of this phase.
