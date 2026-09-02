# Meridian

Meridian is a deterministic C++20 limit order book and matching engine.

The repository has completed Phases 1 through 7, including thread-safe
producer/consumer ingestion with a single matching-engine writer and immutable
snapshots for multiple readers. Networking remains deferred.

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

To run the concurrent path under ThreadSanitizer:

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DMERIDIAN_ENABLE_TSAN=ON -DMERIDIAN_BUILD_BENCHMARKS=OFF
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure
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
