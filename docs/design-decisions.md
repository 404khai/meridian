# Design decisions

## DD-001: Google Benchmark for Phase 6

Phase 6 adds Google Benchmark as a build-time-only dependency, pinned to the
`v1.9.5` commit `192ef10025eb2c4cdd392bc502f0c852196baa48` through CMake
`FetchContent`. The project specification explicitly requires Google Benchmark
for the `meridian_benchmarks` target. It provides calibrated iterations,
timing controls for excluding fixture setup, compiler-optimization barriers,
machine-readable output, and comparable throughput counters without adding
any dependency to `meridian_core` or production executables. Its tests and
installation rules are disabled. Benchmark builds can be omitted with
`-DMERIDIAN_BUILD_BENCHMARKS=OFF` for offline or production-only builds.
