# Phase 5 test matrix

Phase 5 makes tests the primary confidence mechanism for the single-threaded
book and matching engine.

| Area | Coverage |
| --- | --- |
| Empty state | Best bid/ask, depth, lookup, cancel, and empty-book market order |
| Validation | Zero quantity, invalid price, invalid side/type, duplicate IDs |
| Price priority | Best opposing price selected before worse prices |
| Time priority | FIFO within a level and preservation after partial fills |
| Quantity handling | Full fills, partial fills, multiple fills, and market remainders |
| Book operations | Insert, cancel, modify, reduce, depth, and crossed-book invariant |
| Event behavior | Existing Phase 4 typed event and ordering tests |
| Property tests | Deterministic pseudo-random sequences with per-order quantity conservation |
| Determinism | Replaying the same 1,500-order sequence yields equal fills and book state |
| Stress | 10,000 resting orders, bulk cancellation, and 2,000 sequential matches |

The randomized generator uses a fixed seed so failures are reproducible. The
stress cases use synthetic orders and remain single-threaded, as required by
the phase gates in `AGENTS.md`.
