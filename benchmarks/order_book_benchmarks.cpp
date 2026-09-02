#include "meridian/book/order_book.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>

namespace {

using meridian::Order;
using meridian::OrderBook;
using meridian::OrderId;
using meridian::OrderType;
using meridian::Price;
using meridian::Qty;
using meridian::Side;

Order make_order(std::uint64_t id, Price price) {
    return Order{OrderId{id}, Side::Buy, OrderType::Limit, price, Qty{1}};
}

void insert_single_level(benchmark::State& state) {
    const auto order_count = static_cast<std::uint64_t>(state.range(0));
    for (auto _ : state) {
        (void)_;
        state.PauseTiming();
        auto book = std::make_unique<OrderBook>();
        state.ResumeTiming();

        for (std::uint64_t index = 0; index < order_count; ++index) {
            benchmark::DoNotOptimize(book->insert(make_order(index + 1, Price{100})));
        }
        benchmark::ClobberMemory();

        state.PauseTiming();
        book.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(order_count));
}

void insert_many_levels(benchmark::State& state) {
    const auto order_count = static_cast<std::uint64_t>(state.range(0));
    for (auto _ : state) {
        (void)_;
        state.PauseTiming();
        auto book = std::make_unique<OrderBook>();
        state.ResumeTiming();

        for (std::uint64_t index = 0; index < order_count; ++index) {
            const Price price{100 + static_cast<std::int64_t>(index % 1000)};
            benchmark::DoNotOptimize(book->insert(make_order(index + 1, price)));
        }
        benchmark::ClobberMemory();

        state.PauseTiming();
        book.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(order_count));
}

void lookup_hit(benchmark::State& state) {
    const auto order_count = static_cast<std::uint64_t>(state.range(0));
    OrderBook book;
    for (std::uint64_t index = 0; index < order_count; ++index) {
        benchmark::DoNotOptimize(book.insert(make_order(index + 1, Price{100})));
    }

    std::uint64_t index = 0;
    for (auto _ : state) {
        (void)_;
        auto order = book.find(OrderId{(index % order_count) + 1});
        benchmark::DoNotOptimize(order);
        ++index;
    }
    state.SetItemsProcessed(state.iterations());
}

void lookup_miss(benchmark::State& state) {
    const auto order_count = static_cast<std::uint64_t>(state.range(0));
    OrderBook book;
    for (std::uint64_t index = 0; index < order_count; ++index) {
        benchmark::DoNotOptimize(book.insert(make_order(index + 1, Price{100})));
    }

    std::uint64_t index = 0;
    for (auto _ : state) {
        (void)_;
        auto order = book.find(OrderId{order_count + index + 1});
        benchmark::DoNotOptimize(order);
        ++index;
    }
    state.SetItemsProcessed(state.iterations());
}

void cancel_single_level(benchmark::State& state) {
    const auto order_count = static_cast<std::uint64_t>(state.range(0));
    for (auto _ : state) {
        (void)_;
        state.PauseTiming();
        auto book = std::make_unique<OrderBook>();
        for (std::uint64_t index = 0; index < order_count; ++index) {
            benchmark::DoNotOptimize(book->insert(make_order(index + 1, Price{100})));
        }
        (void)book->drain_events();
        state.ResumeTiming();

        for (std::uint64_t index = 0; index < order_count; ++index) {
            benchmark::DoNotOptimize(book->cancel(OrderId{index + 1}));
        }
        benchmark::ClobberMemory();

        state.PauseTiming();
        book.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(order_count));
}

BENCHMARK(insert_single_level)->Name("OrderBook/Insert/SingleLevel")->Arg(1000)->Arg(10000);
BENCHMARK(insert_many_levels)->Name("OrderBook/Insert/ManyLevels")->Arg(1000)->Arg(10000);
BENCHMARK(lookup_hit)->Name("OrderBook/Lookup/Hit")->Arg(10000);
BENCHMARK(lookup_miss)->Name("OrderBook/Lookup/Miss")->Arg(10000);
BENCHMARK(cancel_single_level)->Name("OrderBook/Cancel/SingleLevel")->Arg(1000)->Arg(10000);

} // namespace
