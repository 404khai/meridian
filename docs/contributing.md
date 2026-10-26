# Contributor guide

Read [AGENTS.md](../AGENTS.md) before changes; it defines correctness-first
priorities, tests, phase gates, dependency direction, and commit discipline.
Use descriptive branch/PR names. Conventional Commits may use module or phase
scopes; performance commits require measured before/after evidence.

## Prerequisites and baseline

Use CMake 3.20+ and a C++20 compiler/standard library. CI tests Linux GCC and
macOS Apple Clang. POSIX sockets are required only for networking. Benchmark
fetching needs Git/network access or an existing dependency cache. Python and
Mermaid tools are not required to build/test Meridian.

Run all optional functional suites, without downloading Google Benchmark:

```sh
cmake -S . -B build-all -DCMAKE_BUILD_TYPE=Debug \
  -DMERIDIAN_BUILD_BENCHMARKS=OFF -DMERIDIAN_BUILD_NET=ON
cmake --build build-all --parallel
ctest --test-dir build-all --output-on-failure
```

There are 16 tests with networking and market data enabled, 13 with networking
disabled, and 15 with market data disabled and networking enabled. CTest entries
group multiple assertions/workloads; the entry count is not a coverage percentage.
GCC/Clang builds use `-Wall -Wextra -Wpedantic -Werror` locally as well as in CI.
MSVC warning flags exist, but it is not in the current test matrix.

For explicit layer-exclusion validation:

```sh
cmake -S . -B build-no-adapters -DCMAKE_BUILD_TYPE=Release \
  -DMERIDIAN_BUILD_BENCHMARKS=OFF -DMERIDIAN_BUILD_NET=OFF \
  -DMERIDIAN_BUILD_MARKETDATA=OFF
cmake --build build-no-adapters --parallel
ctest --test-dir build-no-adapters --output-on-failure
```

This configuration has 12 test entries. To build just core without tests or
benchmark fetching:

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DMERIDIAN_BUILD_BENCHMARKS=OFF -DMERIDIAN_BUILD_NET=OFF \
  -DMERIDIAN_BUILD_MARKETDATA=OFF
cmake --build build-core --target meridian_core --parallel
```

## Sanitizers

Use separate directories so instrumented and uninstrumented artifacts are not
mixed. ASan/UBSan and TSan cannot be enabled together.

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DMERIDIAN_BUILD_BENCHMARKS=OFF -DMERIDIAN_BUILD_NET=ON \
  -DMERIDIAN_ENABLE_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DMERIDIAN_BUILD_BENCHMARKS=OFF -DMERIDIAN_BUILD_NET=ON \
  -DMERIDIAN_ENABLE_TSAN=ON
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure
```

CMake propagates public headers/C++20 through source targets; this project has
no installed package/export. Instrumented applications must also compile/link
with compatible sanitizer flags; manually linking an uninstrumented example
to an instrumented static library is not the README quick-start configuration.

## Tests, benchmarks, and evidence

The normal [build workflow](../.github/workflows/build.yml) covers Release,
Debug, ASan/UBSan, TSan, and optional layer exclusion. The
[soak workflow](../.github/workflows/soak.yml) checks Release and Linux
ASan/UBSan/LeakSanitizer runs and uploads summaries. See
[test-matrix.md](test-matrix.md) and [stress-testing.md](stress-testing.md).

Use [benchmark-methodology.md](benchmark-methodology.md) for calibrated batch
measurements and [stress-testing.md](stress-testing.md) for verified load and
latency histograms. Never compare sanitizer timings with Release performance
or substitute batch averages for request percentiles. Retain raw output, source
revision, workload, machine, build flags, and variation; explain regressions too.

For documentation changes, check local links, reconcile claims with current
headers/targets, render Mermaid diagrams, compile/run examples, and build/pass
CTest. Historical benchmark/stress numbers remain tied to their recorded source
revisions; documentation editing does not create new performance evidence.
Do not add implementation-mirroring tests for prose-only changes.

## Review checklist

- Matching changes include behavior tests and preserve deterministic event order.
- Core has no network/market-data includes; `meridian_layering_check` enforces
  direct includes, not every transitive dependency.
- New dependencies/ownership patterns have a recorded rationale; hot-path
  logging is absent unless explicitly flagged.
- Interface/algorithm/concurrency changes update the relevant docs and decision
  record. Public breaking changes get a Conventional Commit footer.
- PRs explain concrete behavior, meaningful validation, and known limits.
- `git diff --check`, the build, and CTest pass before merge.

