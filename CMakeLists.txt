cmake_minimum_required(VERSION 3.20)

project(meridian VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

option(MERIDIAN_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(MERIDIAN_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(MERIDIAN_BUILD_NET "Build the optional POSIX loopback TCP adapter" OFF)
option(MERIDIAN_BUILD_MARKETDATA "Build event-driven market-data publishing" ON)
option(MERIDIAN_BUILD_BENCHMARKS "Build the Google Benchmark suite" ON)

if(MERIDIAN_ENABLE_SANITIZERS AND MERIDIAN_ENABLE_TSAN)
    message(FATAL_ERROR "ASan/UBSan and TSan cannot be enabled together")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(meridian_warnings INTERFACE)
if(MSVC)
    target_compile_options(meridian_warnings INTERFACE /W4 /WX)
else()
    target_compile_options(meridian_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_library(meridian_sanitizers INTERFACE)
if(MERIDIAN_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(meridian_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(meridian_sanitizers INTERFACE -fsanitize=address,undefined)
elseif(MERIDIAN_ENABLE_SANITIZERS)
    message(FATAL_ERROR "ASan/UBSan requires a Clang or GNU-compatible compiler")
elseif(MERIDIAN_ENABLE_TSAN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(meridian_sanitizers INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(meridian_sanitizers INTERFACE -fsanitize=thread)
elseif(MERIDIAN_ENABLE_TSAN)
    message(FATAL_ERROR "TSan requires a Clang or GNU-compatible compiler")
endif()

add_library(meridian_core STATIC
    src/book/order_book.cpp
    src/engine/matching_engine.cpp
)

target_compile_features(meridian_core PUBLIC cxx_std_20)
target_include_directories(meridian_core PUBLIC include)
target_link_libraries(meridian_core PRIVATE meridian_warnings meridian_sanitizers)

if(MERIDIAN_BUILD_MARKETDATA)
    add_library(meridian_marketdata STATIC src/marketdata/market_data_publisher.cpp)
    target_compile_features(meridian_marketdata PUBLIC cxx_std_20)
    target_include_directories(meridian_marketdata PUBLIC include)
    target_link_libraries(meridian_marketdata
        PUBLIC meridian_core
        PRIVATE meridian_warnings meridian_sanitizers
    )
endif()

find_package(Threads REQUIRED)

add_library(meridian_concurrency STATIC
    src/concurrency/concurrent_matching_engine.cpp
)
target_compile_features(meridian_concurrency PUBLIC cxx_std_20)
target_include_directories(meridian_concurrency PUBLIC include)
target_link_libraries(meridian_concurrency
    PUBLIC meridian_core
    PRIVATE meridian_warnings meridian_sanitizers Threads::Threads
)

if(MERIDIAN_BUILD_NET)
    if(NOT UNIX)
        message(FATAL_ERROR "The TCP adapter currently requires POSIX sockets")
    endif()
    add_library(meridian_net STATIC src/net/protocol.cpp src/net/tcp_server.cpp)
    target_compile_features(meridian_net PUBLIC cxx_std_20)
    target_include_directories(meridian_net PUBLIC include)
    target_link_libraries(meridian_net
        PUBLIC meridian_core
        PRIVATE meridian_warnings meridian_sanitizers Threads::Threads)
    add_executable(meridian_tcp_server src/net/main.cpp)
    target_link_libraries(meridian_tcp_server PRIVATE
        meridian_net meridian_warnings meridian_sanitizers)
endif()

add_executable(meridian_engine
    src/engine/main.cpp
)
target_link_libraries(meridian_engine PRIVATE meridian_core meridian_warnings meridian_sanitizers)

if(MERIDIAN_BUILD_BENCHMARKS)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        google_benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG 192ef10025eb2c4cdd392bc502f0c852196baa48
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(google_benchmark)

    add_executable(meridian_benchmarks
        benchmarks/matching_engine_benchmarks.cpp
        benchmarks/order_book_benchmarks.cpp
    )
    target_link_libraries(meridian_benchmarks PRIVATE
        meridian_core
        meridian_warnings
        meridian_sanitizers
        benchmark::benchmark_main
    )
endif()

if(BUILD_TESTING)
    add_executable(meridian_stress tests/stress/stress_runner.cpp)
    target_link_libraries(meridian_stress PRIVATE
        meridian_concurrency meridian_warnings meridian_sanitizers Threads::Threads)
    set(stress_profile "${CMAKE_BUILD_TYPE}")
    if(MERIDIAN_ENABLE_SANITIZERS)
        string(APPEND stress_profile "-asan-ubsan")
    elseif(MERIDIAN_ENABLE_TSAN)
        string(APPEND stress_profile "-tsan")
    endif()
    target_compile_definitions(meridian_stress PRIVATE MERIDIAN_STRESS_PROFILE="${stress_profile}")
    add_test(NAME meridian_stress_self_tests COMMAND meridian_stress --self-test)
    add_test(NAME meridian_stress_matching COMMAND meridian_stress --mode matching --cycles 4 --seed 42)
    add_test(NAME meridian_stress_adversarial COMMAND meridian_stress --mode matching --cycles 4 --seed 8675309)
    add_test(NAME meridian_stress_concurrent COMMAND meridian_stress --mode concurrent --cycles 4)
    set_tests_properties(meridian_stress_self_tests meridian_stress_matching
        meridian_stress_adversarial meridian_stress_concurrent PROPERTIES TIMEOUT 60 LABELS stress)

    if(MERIDIAN_BUILD_NET)
        add_executable(meridian_protocol_adversarial_tests tests/stress/protocol_adversarial.cpp)
        target_link_libraries(meridian_protocol_adversarial_tests PRIVATE
            meridian_net meridian_warnings meridian_sanitizers)
        add_test(NAME meridian_protocol_adversarial_tests COMMAND meridian_protocol_adversarial_tests)
        set_tests_properties(meridian_protocol_adversarial_tests PROPERTIES TIMEOUT 60 LABELS stress)
        add_executable(meridian_protocol_tests tests/unit/protocol_tests.cpp)
        target_link_libraries(meridian_protocol_tests PRIVATE
            meridian_net meridian_warnings meridian_sanitizers)
        add_test(NAME meridian_protocol_tests COMMAND meridian_protocol_tests)
        add_executable(meridian_tcp_tests tests/integration/tcp_server_tests.cpp)
        target_link_libraries(meridian_tcp_tests PRIVATE
            meridian_net meridian_warnings meridian_sanitizers Threads::Threads)
        add_test(NAME meridian_tcp_tests COMMAND meridian_tcp_tests)
        set_tests_properties(meridian_protocol_tests meridian_tcp_tests PROPERTIES TIMEOUT 30)
    endif()

    add_test(NAME meridian_layering_check COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_core_dependencies.cmake)

    if(MERIDIAN_BUILD_MARKETDATA)
        add_executable(meridian_marketdata_tests tests/integration/market_data_tests.cpp)
        target_link_libraries(meridian_marketdata_tests PRIVATE
            meridian_marketdata meridian_warnings meridian_sanitizers Threads::Threads)
        add_test(NAME meridian_marketdata_tests COMMAND meridian_marketdata_tests)
        set_tests_properties(meridian_marketdata_tests PROPERTIES TIMEOUT 30)
    endif()

    add_executable(meridian_tests
        tests/unit/order_book_tests.cpp
    )
    target_link_libraries(meridian_tests PRIVATE meridian_core meridian_warnings meridian_sanitizers)
    add_test(NAME meridian_tests COMMAND meridian_tests)

    add_executable(meridian_matching_tests
        tests/unit/matching_engine_tests.cpp
    )
    target_link_libraries(meridian_matching_tests PRIVATE meridian_core meridian_warnings meridian_sanitizers)
    add_test(NAME meridian_matching_tests COMMAND meridian_matching_tests)

    add_executable(meridian_modification_tests
        tests/unit/order_modification_tests.cpp
    )
    target_link_libraries(meridian_modification_tests PRIVATE meridian_core meridian_warnings meridian_sanitizers)
    add_test(NAME meridian_modification_tests COMMAND meridian_modification_tests)

    add_executable(meridian_event_tests
        tests/unit/event_tests.cpp
    )
    target_link_libraries(meridian_event_tests PRIVATE meridian_core meridian_warnings meridian_sanitizers)
    add_test(NAME meridian_event_tests COMMAND meridian_event_tests)

    add_executable(meridian_comprehensive_tests
        tests/integration/phase5_comprehensive_tests.cpp
    )
    target_link_libraries(meridian_comprehensive_tests PRIVATE meridian_core meridian_warnings meridian_sanitizers)
    add_test(NAME meridian_comprehensive_tests COMMAND meridian_comprehensive_tests)

    add_executable(meridian_queue_tests
        tests/unit/blocking_queue_tests.cpp
    )
    target_link_libraries(meridian_queue_tests PRIVATE
        meridian_concurrency meridian_warnings meridian_sanitizers Threads::Threads
    )
    add_test(NAME meridian_queue_tests COMMAND meridian_queue_tests)
    set_tests_properties(meridian_queue_tests PROPERTIES TIMEOUT 30)

    add_executable(meridian_concurrency_tests
        tests/integration/concurrent_ingestion_tests.cpp
    )
    target_link_libraries(meridian_concurrency_tests PRIVATE
        meridian_concurrency meridian_warnings meridian_sanitizers Threads::Threads
    )
    add_test(NAME meridian_concurrency_tests COMMAND meridian_concurrency_tests)
    set_tests_properties(meridian_concurrency_tests PROPERTIES TIMEOUT 30)
endif()
