# AGENTS.md — Meridian

This file defines how any agent (human or AI) should work on Meridian: a
production-oriented, low-latency limit order book and matching engine in
C++20. It is the source of truth for phase sequencing, scope boundaries,
coding conventions, and commit discipline. Read this before opening a PR
or starting a new phase.

---

## 0. Project Identity

- **Name:** Meridian
- **Type:** Systems-engineering project (matching engine + infra), not a UI project
- **Language/Standard:** C++20
- **Build system:** CMake
- **North star:** correctness and determinism first, performance second,
  concurrency only after single-threaded semantics are proven.

### Priority order (never violate this order)
1. Correctness
2. Determinism
3. Clear architecture
4. Measurable performance
5. Concurrency safety
6. Efficient memory usage
7. Maintainability

If a change improves something lower on this list at the expense of
something higher, it does not get merged without an explicit, documented
justification in the PR description.

---

## 1. Golden Rules for Agents

1. **Never introduce concurrency before single-threaded matching is
   correct and tested.** This is a hard gate, not a suggestion — see
   Phase gating below.
2. **Never optimize on intuition.** Every performance-motivated change
   needs a benchmark (before/after) attached to the PR. "This should be
   faster" is not acceptable justification.
3. **Never let networking or presentation-layer concerns leak into the
   core matching engine.** The core library must compile and run with
   zero knowledge of sockets, protocols, or I/O.
4. **Never add a dependency without justification.** Default to the
   standard library. A new dependency requires a short "why" note in the
   PR and in `docs/design-decisions.md`.
5. **Never log on the hot path** (order submission → match → event
   emission) unless logging is explicitly compiled/flagged in.
6. **Always keep the build warning-clean.** Treat new warnings as build
   failures (`-Wall -Wextra -Wpedantic -Werror` in CI; local dev builds
   may relax `-Werror`).
7. **Always write tests in the same PR as the feature**, not after.
   Matching-logic PRs without tests are rejected regardless of how
   "obviously correct" the code looks.
8. **Prefer value semantics, explicit ownership, and strong types** over
   inheritance/dynamic dispatch. If you reach for a virtual function or a
   `shared_ptr` for something that doesn't need shared ownership, justify
   it in the PR.

---

## 2. Repository Layout (target shape)

```
meridian/
├── AGENTS.md
├── README.md
├── CMakeLists.txt
├── cmake/                  # toolchain / warning / sanitizer presets
├── include/meridian/       # public headers, mirrors src/ module structure
├── src/
│   ├── core/                # domain types: OrderId, Price, Qty, Side, enums
│   ├── order/                # Order, OrderManager
│   ├── book/                 # OrderBook, price levels
│   ├── engine/                # MatchingEngine, matching rules
│   ├── events/                # Trade/event generation, event queue
│   ├── concurrency/           # queues, atomics wrappers, sync primitives
│   ├── marketdata/            # snapshot/publishing layer
│   └── net/                   # optional TCP/protocol layer (isolated)
├── benchmarks/               # Google Benchmark targets
├── tests/
│   ├── unit/
│   └── integration/
└── docs/
    ├── architecture.md
    ├── matching-algorithm.md
    ├── concurrency-model.md
    ├── data-structures.md
    ├── memory-strategy.md
    ├── benchmark-methodology.md
    ├── benchmark-results.md
    └── design-decisions.md
```

Core matching engine (`core/`, `order/`, `book/`, `engine/`, `events/`)
must never `#include` anything from `net/` or `marketdata/`. CI should
enforce this with a dependency-direction check once the project has more
than a couple modules (e.g. `include-what-you-use` or a simple grep-based
layering test).

---

## 3. Build & Test Baseline (set up before Phase 1 code lands)

- CMake targets, minimum:
  - `meridian_core` (static/shared library — no networking, no I/O beyond logging hooks)
  - `meridian_engine` (executable — CLI/file-driven simulation)
  - `meridian_tests` (unit + integration, via CTest)
  - `meridian_benchmarks` (Google Benchmark)
- Compiler flags: `-std=c++20 -Wall -Wextra -Wpedantic`, `-Werror` in CI.
- Sanitizers: ASan + UBSan build variant available from Phase 1; TSan
  build variant required before Phase 7 (concurrent ingestion) lands.
- Every PR must build clean and pass `ctest` before merge.

---

## 4. Development Phases

Work proceeds strictly in this order. A phase is not "done enough to move
on" until its exit criteria are met. Do not start phase *N+1* work in the
same branch/PR as phase *N* unless explicitly noted.

### Phase 1 — Single-threaded Order Book
**Scope:** Core domain types (`OrderId`, `Price`, `Qty`, `Side`, `OrderType`
as `enum class`), `Order`, and `OrderBook` with price-level storage
(e.g. `std::map<Price, PriceLevel>` per side) and O(1) order lookup by ID
(`std::unordered_map<OrderId, ...>`). No matching yet — just insert,
cancel, and book-state queries (best bid/ask, depth at level).
**Exit criteria:**
- Book supports insert/cancel/query with correct price-time ordering.
- Unit tests cover insertion, cancellation, empty-book edge cases.
- No matching logic yet, no concurrency, no networking.

### Phase 2 — Matching Engine
**Scope:** Price-time priority matching for limit and market orders,
partial and full fills, deterministic tie-breaking.
**Exit criteria:**
- Matching rules from the spec (better price first, FIFO within price,
  partial fills preserve remaining qty + original time priority) are
  implemented and covered by tests.
- Matching is proven deterministic (same input sequence → same output
  sequence, verified by a repeatable test).

### Phase 3 — Cancellation / Modification
**Scope:** Order modification (price/qty changes with correct
priority-loss semantics — e.g. qty-decrease keeps priority, price change
or qty-increase loses priority), robust cancellation including
already-partially-filled orders.
**Exit criteria:** Modification semantics documented and tested,
including edge cases (cancel of non-existent order, modify after full
fill, etc.).

### Phase 4 — Event Generation
**Scope:** Trade/event objects, an event queue/log for order-accepted,
order-rejected, order-matched, trade-generated events.
**Exit criteria:** Every state-changing book operation emits a
well-defined event; events are queryable/inspectable in tests.

### Phase 5 — Comprehensive Tests
**Scope:** Fill out the full test matrix from the spec: price priority,
time priority, partial/multiple fills, invalid orders, empty books,
property-based tests for matching invariants (e.g. total qty conserved),
stress tests with large synthetic order sequences.
**Exit criteria:** Test suite is the primary confidence mechanism going
into benchmarking and concurrency; coverage of matching/book logic should
be treated as release-blocking from this point forward.

### Phase 6 — Benchmarks (single-threaded baseline)
**Scope:** Google Benchmark suite for single-threaded matching, order
lookup, cancellation, insertion, across candidate data structures.
**Exit criteria:** Baseline numbers recorded in `docs/benchmark-results.md`
before any optimization work starts. This baseline is the comparison
point for every future performance PR.

### Phase 7 — Concurrent Ingestion — **hard gate: Phases 1–6 must be complete and green**
**Scope:** Thread-safe order submission, producer/consumer queues for
inbound orders, single-writer/multiple-reader exploration.
**Exit criteria:** Concurrent submission path has TSan-clean tests, race
conditions covered by stress tests, and a documented concurrency model in
`docs/concurrency-model.md`.

### Phase 8 — Performance Optimization
**Scope:** Lock-free structures, custom allocators/pools, atomics — only
where Phase 6/7 benchmarks show a justified need.
**Exit criteria:** Every optimization PR includes a before/after
benchmark. No optimization merges on the basis of intuition alone
(Golden Rule #2).

### Phase 9 — Market-Data Interface
**Scope:** Snapshot publishing (best bid/ask, spread, depth), throughput
and latency counters exposed for observability. Still no networking.
**Exit criteria:** Market-data layer consumes engine events without the
core engine knowing the market-data layer exists.

### Phase 10 — Networking
**Scope:** Optional TCP layer, simple binary or text protocol, strictly
isolated in `src/net/`.
**Exit criteria:** Core library builds and tests pass with `net/` fully
excluded from the build. Networking failures cannot crash or corrupt
engine state.

### Phase 11 — Stress Testing
**Scope:** Sustained high-throughput runs, long-duration soak tests,
adversarial input generation (malformed/edge-case order sequences).
**Exit criteria:** No leaks (ASan-clean over long runs), no missed/duplicated
events, latency percentiles recorded under load.

### Phase 12 — Production-Quality Documentation
**Scope:** Architecture diagrams, example order flows, full benchmark
methodology and results, design tradeoffs, known limitations.
**Exit criteria:** README is self-sufficient for a new contributor or
interviewer to understand the system without reading source first.

---

## 5. Commit Message Convention

Meridian uses **Conventional Commits**, with a `phaseN` scope tag so the
project history reads as a phase-by-phase build log — useful both for
your own review and for walking an interviewer through the commit log.

```
<type>(<scope>): <short summary, imperative mood, ≤72 chars>

[optional body: what changed and why, wrap at 72 chars]

[optional footer: benchmark deltas, breaking changes, issue refs]
```

### Types
| Type       | Use for |
|------------|---------|
| `feat`     | new functionality (a new matching rule, a new data structure) |
| `fix`      | bug fix in existing behavior |
| `perf`     | performance optimization — **must include benchmark evidence in the body** |
| `test`     | adding or updating tests only |
| `bench`    | adding/updating benchmark harnesses (not the same as `perf`) |
| `refactor` | no behavior change, structural/code-quality change |
| `docs`     | documentation only |
| `build`    | CMake, compiler flags, CI config |
| `chore`    | repo maintenance, no source impact |

### Scope
Use the phase and/or module, whichever is more informative:
- Phase form: `phase1`, `phase2`, … `phase12`
- Module form: `book`, `engine`, `order`, `events`, `concurrency`,
  `marketdata`, `net`, `bench`, `tests`

Combine when useful: `feat(phase2/engine): implement price-time matching`

### Rules
1. Subject line imperative, no trailing period: `feat(phase1/book): add O(1) cancel via order-id map`, not `Added cancel support.`
2. One logical change per commit. A commit that touches both `book/` and
   `net/` in an unrelated way should be split.
3. `perf` commits **must** state the benchmark delta in the body, e.g.:
   ```
   perf(phase8/book): replace std::map price levels with flat vector

   Benchmark: insert p50 340ns -> 110ns, p99 1.2us -> 340ns
   (10k orders, single-threaded, Release build, see docs/benchmark-results.md)
   ```
4. Commits that cross a phase gate (e.g. introducing threading before
   Phase 7 exit criteria are met) are not allowed — call this out in
   review if seen.
5. Breaking changes to public headers in `include/meridian/` get a
   `BREAKING CHANGE:` footer line.

### Examples
```
feat(phase1/core): add strong types for Price, Qty, OrderId

test(phase2/engine): cover partial-fill time-priority preservation

fix(phase3/book): correct priority loss on quantity-increase modify

perf(phase8/engine): intrusive linked list for price-level orders

Benchmark: cancel p50 210ns -> 40ns (see docs/benchmark-results.md)

docs(phase12): add matching algorithm walkthrough with example order flow

build: enable ASan+UBSan variant target

BREAKING CHANGE: none
```

---

## 6. Branching

- `main` — always buildable, always green (build + full test suite).
- `phaseN/<short-description>` — feature branches per phase, e.g.
  `phase2/price-time-matching`.
- No phase's branch merges into `main` until its exit criteria (Section 4)
  are satisfied and CI is green, including the relevant sanitizer variant.

---

## 7. Definition of Done (applies to every PR, every phase)

A PR is mergeable only if all of the following hold:
- [ ] Builds warning-clean with the phase-appropriate flags.
- [ ] Unit tests included and passing; integration tests updated if the
      change affects cross-module behavior.
- [ ] No core-engine → networking/presentation dependency introduced.
- [ ] No new dependency without a one-paragraph justification in the PR.
- [ ] No hot-path logging added without an explicit compile-time/runtime flag.
- [ ] Performance-motivated changes include a benchmark before/after.
- [ ] Relevant `docs/*.md` updated if architecture, algorithm, or
      concurrency model changed.
- [ ] Commit messages follow Section 5.

---

## 8. Non-Goals (explicitly out of scope unless the spec changes)

- No UI/GUI layer.
- No persistence/database layer unless a future phase explicitly adds one.
- No multi-asset-class abstraction — single instrument order book is the
  target unless extended later.
- No premature lock-free code before Phase 8, and never without benchmark
  justification even then.