# Meridian

Meridian is a deterministic C++20 limit order book and matching engine.

The repository is currently implementing Phase 1 from `AGENTS.md`: a
single-threaded order book with insert, cancel, price-level queries, and
price-time ordering. Matching, modification, events, networking, and
concurrency are intentionally not implemented yet.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

To build with AddressSanitizer and UBSan:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DMERIDIAN_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```
