cmake_minimum_required(VERSION 3.20)

project(meridian VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(MERIDIAN_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

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
endif()

add_library(meridian_core STATIC
    src/book/order_book.cpp
    src/engine/matching_engine.cpp
)

target_compile_features(meridian_core PUBLIC cxx_std_20)
target_include_directories(meridian_core PUBLIC include)
target_link_libraries(meridian_core PRIVATE meridian_warnings meridian_sanitizers)

add_executable(meridian_engine
    src/engine/main.cpp
)
target_link_libraries(meridian_engine PRIVATE meridian_core meridian_warnings meridian_sanitizers)

add_executable(meridian_benchmarks
    benchmarks/placeholder.cpp
)
target_link_libraries(meridian_benchmarks PRIVATE meridian_core meridian_warnings meridian_sanitizers)

if(BUILD_TESTING)
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
endif()
