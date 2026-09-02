#include "meridian/engine/matching_engine.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>

namespace {

using meridian::MatchingEngine;
using meridian::Order;
using meridian::OrderId;
using meridian::OrderType;
using meridian::Price;
using meridian::Qty;
using meridian::Side;

Order limit_order(std::uint64_t id, Side side, Price price) {
    return Order{OrderId{id}, side, OrderType::Limit, price, Qty{1}};
}

Order market_order(std::uint64_t id, Side side, std::uint64_t quantity) {
    return Order{OrderId{id}, side, OrderType::Market, Price{0}, Qty{quantity}};
}

void match_single_level(benchmark::State& state) {
    const auto order_count = static_cast<std::uint64_t>(state.range(0));
    for (auto _ : state) {
        (void)_;
        state.PauseTiming();
        auto engine = std::make_unique<MatchingEngine>();
        for (std::uint64_t index = 0; index < order_count; ++index) {
            benchmark::DoNotOptimize(
                engine->submit(limit_order(index + 1, Side::Sell, Price{100})).accepted);
        }
        (void)engine->drain_events();
        state.ResumeTiming();

        auto result = engine->submit(
            market_order(order_count + 1, Side::Buy, order_count));
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();

        state.PauseTiming();
        engine.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(order_count));
}

void match_many_levels(benchmark::State& state) {
    const auto order_count = static_cast<std::uint64_t>(state.range(0));
    for (auto _ : state) {
        (void)_;
        state.PauseTiming();
        auto engine = std::make_unique<MatchingEngine>();
        for (std::uint64_t index = 0; index < order_count; ++index) {
            const Price price{100 + static_cast<std::int64_t>(index % 100)};
            benchmark::DoNotOptimize(
                engine->submit(limit_order(index + 1, Side::Sell, price)).accepted);
        }
        (void)engine->drain_events();
        state.ResumeTiming();

        auto result = engine->submit(
            market_order(order_count + 1, Side::Buy, order_count));
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();

        state.PauseTiming();
        engine.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(order_count));
}

BENCHMARK(match_single_level)->Name("Matching/MarketSweep/SingleLevel")->Arg(100)->Arg(1000);
BENCHMARK(match_many_levels)->Name("Matching/MarketSweep/ManyLevels")->Arg(100)->Arg(1000);

} // namespace
