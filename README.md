# Meridian

Meridian is a deterministic C++20 limit order book and matching engine.

The repository has completed Phases 1 through 4 and is implementing Phase 5
from `AGENTS.md`: comprehensive correctness, invariant, deterministic replay,
and stress testing. Networking and concurrency remain intentionally deferred.

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
