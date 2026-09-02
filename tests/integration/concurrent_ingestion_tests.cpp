#include "meridian/concurrency/concurrent_matching_engine.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

using meridian::ConcurrentMatchingEngine;
using meridian::Order;
using meridian::OrderId;
using meridian::OrderType;
using meridian::Price;
using meridian::Qty;
using meridian::Side;
using meridian::SubmitResult;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

Order limit_order(std::uint64_t id, Side side, std::int64_t price,
                  std::uint64_t quantity = 1) {
    return Order{OrderId{id}, side, OrderType::Limit, Price{price}, Qty{quantity}};
}

Order market_order(std::uint64_t id, Side side, std::uint64_t quantity = 1) {
    return Order{OrderId{id}, side, OrderType::Market, Price{0}, Qty{quantity}};
}

void concurrent_producers_and_readers_are_consistent() {
    constexpr std::size_t producer_count = 8;
    constexpr std::size_t orders_per_producer = 500;
    constexpr std::size_t reader_count = 4;
    constexpr std::size_t total_orders = producer_count * orders_per_producer;

    ConcurrentMatchingEngine engine;
    std::vector<std::vector<std::future<SubmitResult>>> futures(producer_count);
    std::atomic<bool> stop_readers{false};
    std::atomic<bool> reader_failed{false};

    const auto initial_view = engine.snapshot();
    require(initial_view->processed_submissions == 0 && initial_view->order_count == 0,
            "initial snapshot is empty");

    std::vector<std::thread> readers;
    readers.reserve(reader_count);
    for (std::size_t reader = 0; reader < reader_count; ++reader) {
        readers.emplace_back([&] {
            std::uint64_t last_processed = 0;
            while (!stop_readers.load(std::memory_order_acquire)) {
                const auto view = engine.snapshot();
                if (view->processed_submissions < last_processed) {
                    reader_failed.store(true, std::memory_order_relaxed);
                }
                if (view->best_bid.has_value() && view->best_ask.has_value() &&
                    *view->best_bid >= *view->best_ask) {
                    reader_failed.store(true, std::memory_order_relaxed);
                }
                last_processed = view->processed_submissions;
            }
        });
    }

    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            auto& producer_futures = futures[producer];
            producer_futures.reserve(orders_per_producer);
            const Side side = producer < producer_count / 2 ? Side::Buy : Side::Sell;
            const std::int64_t price = side == Side::Buy ? 90 : 110;
            for (std::size_t offset = 0; offset < orders_per_producer; ++offset) {
                const auto id = static_cast<std::uint64_t>(
                    producer * orders_per_producer + offset + 1);
                producer_futures.push_back(engine.submit(limit_order(id, side, price)));
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }
    for (auto& producer_futures : futures) {
        for (auto& future : producer_futures) {
            const auto result = future.get();
            require(result.accepted, "concurrent valid submission is accepted");
            require(result.fills.empty(), "non-crossing concurrent order does not match");
            require(result.remaining == Qty{1}, "concurrent limit order rests in full");
        }
    }

    stop_readers.store(true, std::memory_order_release);
    for (auto& reader : readers) {
        reader.join();
    }

    require(!reader_failed.load(std::memory_order_relaxed),
            "all readers observe coherent monotonic snapshots");
    const auto final_view = engine.snapshot();
    require(final_view->processed_submissions == total_orders,
            "single writer processes every submission");
    require(final_view->order_count == total_orders, "every non-crossing order is visible");
    require(final_view->best_bid == std::optional<Price>{Price{90}},
            "concurrent book has expected best bid");
    require(final_view->best_ask == std::optional<Price>{Price{110}},
            "concurrent book has expected best ask");
    require(final_view->bid_level_count == 1 && final_view->ask_level_count == 1,
            "snapshot level counts are coherent");
}

void concurrent_aggressors_fill_each_resting_order_once() {
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t orders_per_producer = 500;
    constexpr std::size_t total_orders = producer_count * orders_per_producer;

    ConcurrentMatchingEngine engine;
    std::vector<std::future<SubmitResult>> resting;
    resting.reserve(total_orders);
    for (std::size_t index = 0; index < total_orders; ++index) {
        resting.push_back(engine.submit(limit_order(
            static_cast<std::uint64_t>(10000 + index), Side::Sell, 100)));
    }
    for (auto& future : resting) {
        require(future.get().accepted, "resting liquidity is accepted");
    }

    std::vector<std::vector<std::future<SubmitResult>>> futures(producer_count);
    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            auto& producer_futures = futures[producer];
            producer_futures.reserve(orders_per_producer);
            for (std::size_t offset = 0; offset < orders_per_producer; ++offset) {
                const auto id = static_cast<std::uint64_t>(
                    20000 + producer * orders_per_producer + offset);
                producer_futures.push_back(engine.submit(market_order(id, Side::Buy)));
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    std::unordered_set<std::uint64_t> resting_ids;
    resting_ids.reserve(total_orders);
    for (auto& producer_futures : futures) {
        for (auto& future : producer_futures) {
            const auto result = future.get();
            require(result.accepted, "concurrent market order is accepted");
            require(result.fills.size() == 1, "concurrent market order has one fill");
            require(result.remaining == Qty{0}, "concurrent market order fills completely");
            resting_ids.insert(result.fills.front().resting_id.value());
        }
    }

    require(resting_ids.size() == total_orders, "each resting order fills exactly once");
    const auto final_view = engine.snapshot();
    require(final_view->order_count == 0, "concurrent matching drains resting liquidity");
    require(final_view->processed_submissions == total_orders * 2,
            "single writer processes makers and aggressors");
}

void duplicate_ids_are_serially_rejected() {
    constexpr std::size_t producer_count = 8;
    ConcurrentMatchingEngine engine;
    std::vector<std::future<SubmitResult>> futures(producer_count);
    std::vector<std::thread> producers;
    producers.reserve(producer_count);

    for (std::size_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            futures[producer] = engine.submit(limit_order(50000, Side::Buy, 90));
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    std::size_t accepted = 0;
    for (auto& future : futures) {
        if (future.get().accepted) {
            ++accepted;
        }
    }
    require(accepted == 1, "single writer accepts exactly one duplicate ID");
    require(engine.snapshot()->order_count == 1, "duplicate rejection preserves one order");
}

void destruction_drains_accepted_work() {
    constexpr std::size_t order_count = 1000;
    std::vector<std::future<SubmitResult>> futures;
    futures.reserve(order_count);
    {
        auto engine = std::make_unique<ConcurrentMatchingEngine>();
        for (std::size_t index = 0; index < order_count; ++index) {
            futures.push_back(engine->submit(limit_order(
                static_cast<std::uint64_t>(60000 + index), Side::Buy, 90)));
        }
    }

    for (auto& future : futures) {
        require(future.get().accepted, "destructor drains queued submissions before joining");
    }
}

} // namespace

int main() {
    concurrent_producers_and_readers_are_consistent();
    concurrent_aggressors_fill_each_resting_order_once();
    duplicate_ids_are_serially_rejected();
    destruction_drains_accepted_work();
    std::cout << "All Meridian Phase 7 concurrent-ingestion tests passed\n";
    return EXIT_SUCCESS;
}
