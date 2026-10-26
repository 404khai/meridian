# Loopback TCP order entry

The optional `meridian_net` library and `meridian_tcp_server` executable provide
POSIX IPv4 TCP order entry on **127.0.0.1 only**. All socket and protocol code
lives in `src/net/`, with public adapter headers in `include/meridian/net/`.
The core library has no network dependency. No third-party library is added.

## Build and run

Networking defaults to disabled. Enable it explicitly:

```sh
cmake -S . -B build-net -DCMAKE_BUILD_TYPE=Release \
  -DMERIDIAN_BUILD_NET=ON -DMERIDIAN_BUILD_BENCHMARKS=OFF
cmake --build build-net --parallel
ctest --test-dir build-net --output-on-failure
./build-net/meridian_tcp_server 9000
```

Port zero asks the OS for an ephemeral port. The executable prints the bound
address at startup and exits on SIGINT/SIGTERM. A bind or listener failure is
reported as a controlled error. There is no per-order logging. Windows builds
must leave networking disabled; the core remains portable.

For a manual session, connect `nc 127.0.0.1 9000` and enter the lines below.
The server owns one in-memory matching engine shared across connections.

## Request grammar

Each request ends with LF; CRLF is accepted. Commands and sides are uppercase.
Leading/trailing ASCII spaces or tabs and repeated separators are accepted.
Fields must be complete unsigned decimal integers, without signs, suffixes, or
embedded control characters. Price is an integer tick value, not a decimal
currency amount. Leading zeroes are allowed.

```text
PING
BOOK
LIMIT <id> <BUY|SELL> <price> <quantity>
MARKET <id> <BUY|SELL> <quantity>
```

- ID: 0 through 18446744073709551615, scoped to currently resting orders,
  consistent with core validation. IDs become reusable after complete execution.
- Limit price: 1 through 9223372036854775807 ticks.
- Quantity: 1 through 1000000000000 units.
- Maximum resting orders: 100000. At capacity all limit submissions are
  rejected before matching, even if they might remove liquidity. Market orders
  remain available to reduce the book. This conservative admission policy and
  quantity limit keep aggregate resting quantity below `uint64_t` overflow.

Cancellation/modification, market-data streaming, authentication, remote bind
addresses, persistence, and protocol negotiation are outside this interface.

## Responses

```text
PONG
BOOK <best-bid|NONE> <best-ask|NONE> <resting-order-count>
ERR BAD_REQUEST
ERR BOOK_FULL
REJECT <id>
```

`REJECT` denotes a core rejection (a duplicate currently resting ID). Malformed
commands and numeric/domain bounds produce `ERR BAD_REQUEST` without entering
the engine. An error line does not desynchronize subsequent requests.

Accepted orders receive one header, zero or more fills, and a terminator:

```text
OK <id> <remaining-quantity> <fill-count>
FILL <aggressor-id> <resting-id> <maker-price> <fill-quantity>
END
```

Fills preserve price-time order and execute at maker prices. Limit remainders
rest; market remainders are reported and discarded. Only the submitting client
receives its response; there are no asynchronous maker notifications.

Example, on the same connection:

```text
LIMIT 1 SELL 100 3
OK 1 3 0
END
LIMIT 2 BUY 100 2
OK 2 0 1
FILL 2 1 100 2
END
BOOK
BOOK NONE 100 1
```

The response to a request is serialized in full before any of that response is
queued. Normal socket writes may still split the queued bytes arbitrarily;
clients must frame by lines and consume the advertised fill count plus `END`.

## Framing, resource limits, and failure semantics

TCP read boundaries are unrelated to commands. The server buffers partial lines
and accepts multiple lines in a read. A connection ending halfway through a
request discards that incomplete line. A write-half-close after complete
requests still receives queued replies before EOF.

Per server, at most 64 connections are admitted. Excess connections close. Each
connection buffers at most 256 request bytes (excluding LF, including optional
CR) and 65536 unsent response bytes. A longer line or a response exceeding the
remaining output budget closes that connection. Serialization stops when the
response budget is exhausted. Previously queued responses may be lost on such
a closure. Kernel TCP buffers are separately managed by the OS. A delayed
reader/writer cannot block the event loop; idle clients can occupy a slot until
they disconnect or the server stops. There is no idle-timeout policy.

A **complete valid command may execute even if its response cannot be delivered**
(e.g. reset, oversized fill response, or output backpressure). Disconnecting
never cancels or rolls back an executed order. Client EOF/reset/timeout is an
uncertain result, not a rejection. Automatic retries are not exactly-once:
resting-ID deduplication is not a durable request ledger, and filled IDs are
reusable. Query or reconcile application state before any retry.

The server suppresses SIGPIPE per socket/send without changing the process-wide
SIGPIPE disposition. It handles short sends, short receives, EAGAIN, EINTR,
resets, and orderly EOF. Client errors close only the affected descriptor;
listener setup/poll failure reports an exception to the owning application.
`main` catches exceptions and exits cleanly. Unexpected core/allocation exceptions
are not converted into retriable network rejections; they propagate to the owner.
There is no claim to recover matching from allocation failure.

## Ownership and integration

`TcpServer` owns a `ProtocolSession` and its `MatchingEngine`. Exactly one thread
calls `poll_once` or `run(stop_token)`. All commands execute serially on that
thread; per-connection order is FIFO. Cross-connection ordering is determined by
the event loop and is not a client-arrival-time guarantee. The same concrete
execution sequence retains the core's deterministic behavior.

Accepted sockets are nonblocking and close-on-exec. Polling is bounded to 50 ms
in `run`; reads and accepts have bounded work per iteration. The owning
application stops and joins its worker before destroying the server. `port()`
is immutable after construction and can be used by a client thread. Other
server/session methods do not support concurrent callers.

The adapter drains the event log after each submission, so it does not accumulate
an unbounded event history. It currently neither fans out events to market-data
consumers nor uses the separate concurrent-ingestion adapter. Core headers and
behavior are unchanged. Future applications can add event fanout at this outer
ownership boundary; it does not belong inside core matching.

## Isolation and validation

Configure `-DMERIDIAN_BUILD_NET=OFF` to exclude all network sources and targets.
Core, matching, modification, event, concurrency, and market-data tests still
run. The existing CTest layering check rejects core includes of `net/` or
`marketdata/`; CI exercises enabled, disabled, and sanitizer configurations.

Protocol tests cover malformed/overflowing numbers and control bytes, exact
fills and replay, admission capacity, and response-budget behavior. Loopback
integration tests cover fragmented/pipelined requests, shared state between
clients, half-close, reset, incomplete input, oversized lines/replies, delayed
readers, excess connections, bind failure, and stopping with a stalled client.
These are functional failure tests, not throughput or long-duration soak claims.
