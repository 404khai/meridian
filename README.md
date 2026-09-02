# Meridian

Meridian is a deterministic C++20 limit order book and matching engine.

The repository has completed Phases 1 through 6, including reproducible
single-threaded performance baselines for the book and matching engine.
Networking and concurrency remain intentionally deferred until the Phase 7
gate is entered.

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

## Benchmarks

Google Benchmark is fetched at its pinned Phase 6 revision. Build and run the
optimized suite with:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target meridian_benchmarks --parallel
./build-release/meridian_benchmarks
```

See `docs/benchmark-methodology.md` and `docs/benchmark-results.md` for the
reproducible run configuration and recorded baseline.
