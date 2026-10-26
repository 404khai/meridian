#include "meridian/marketdata/market_data_publisher.hpp"
#include "meridian/book/order_book.hpp"
#include "meridian/engine/matching_engine.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace meridian;
using namespace std::chrono_literals;
using Clock = MarketDataPublisher::Clock;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

Order limit(std::uint64_t id, Side side, std::int64_t price, std::uint64_t qty) {
    return Order{OrderId{id}, side, OrderType::Limit, Price{price}, Qty{qty}};
}

void compare(const MarketDataSnapshot& snapshot, const OrderBook& book) {
    require(snapshot.best_bid == book.best_bid() && snapshot.best_ask == book.best_ask(),
            "event projection best prices agree with source");
    require(snapshot.order_count == book.order_count(), "projected order count agrees");
    require(snapshot.bids.size() == book.level_count(Side::Buy) &&
                snapshot.asks.size() == book.level_count(Side::Sell), "level counts agree");
    for (const auto side : {Side::Buy, Side::Sell}) {
        const auto& levels = side == Side::Buy ? snapshot.bids : snapshot.asks;
        for (std::size_t i = 0; i < levels.size(); ++i) {
            const auto& level = levels[i];
            require(book.depth_at(side, level.price) == level.quantity, "depth agrees");
            require(book.orders_at(side, level.price).size() == level.order_count,
                    "level order count agrees");
            if (i > 0) {
                require(side == Side::Buy ? levels[i - 1].price > level.price
                                         : levels[i - 1].price < level.price,
                        "depth sorted best first");
            }
        }
    }
    if (snapshot.best_bid && snapshot.best_ask) {
        require(snapshot.spread_ticks == snapshot.best_ask->value() - snapshot.best_bid->value(),
                "spread measured in signed ticks");
    } else {
        require(!snapshot.spread_ticks, "spread absent with an empty side");
    }
}

void direct_book_events_and_counters() {
    OrderBook book;
    MarketDataPublisher publisher{Clock::time_point{}};
    const auto initial = publisher.snapshot();
    require(initial->sequence == 0 && !initial->best_bid && !initial->best_ask &&
                initial->counters.operations_per_second() == 0.0 &&
                initial->counters.mean_latency_ns() == 0.0, "initial snapshot is empty");
    std::uint64_t operations = 0;
    const auto publish = [&] {
        const auto events = book.drain_events();
        ++operations;
        publisher.publish(events, std::chrono::nanoseconds{static_cast<long long>(operations * 10)},
                          Clock::time_point{} + std::chrono::seconds{operations});
        compare(*publisher.snapshot(), book);
    };
    require(book.insert(limit(1, Side::Buy, 100, 10)), "insert bid"); publish();
    const auto retained = publisher.snapshot();
    require(book.insert(limit(2, Side::Buy, 100, 5)), "insert same level"); publish();
    require(book.insert(limit(3, Side::Sell, 105, 7)), "insert ask"); publish();
    require(publisher.snapshot()->spread_ticks == 5, "two-sided spread");
    require(book.modify(OrderId{1}, Price{101}, Qty{12}), "move price and increase"); publish();
    require(book.modify(OrderId{1}, Price{101}, Qty{9}), "decrease quantity"); publish();
    require(book.reduce_quantity(OrderId{1}, Qty{4}), "partial reduction"); publish();
    require(book.reduce_quantity(OrderId{1}, Qty{5}), "full reduction"); publish();
    require(book.cancel(OrderId{2}), "cancel last bid"); publish();
    require(!book.insert(limit(3, Side::Buy, 100, 1)), "duplicate rejected"); publish();
    require(book.cancel(OrderId{3}), "remove final ask"); publish();
    require(!book.cancel(OrderId{999}), "unknown cancellation has no event"); publish();
    const auto current = publisher.snapshot();
    require(current->sequence == 11 && current->counters.operations == 11 &&
                current->counters.events == 10 && current->counters.accepted_orders == 3 &&
                current->counters.rejected_orders == 1 && current->counters.trades == 0,
            "operation and event counters include empty operations");
    require(current->counters.total_latency == 660ns && current->counters.min_latency == 10ns &&
                current->counters.max_latency == 110ns &&
                current->counters.mean_latency_ns() == 60.0 &&
                current->counters.operations_per_second() == 1.0, "exact latency and throughput");
    require(retained->order_count == 1 && retained->bids[0].quantity == Qty{10} &&
                retained->sequence == 1 && initial->sequence == 0,
            "retained snapshots remain immutable");
}

void matching_batches_and_replay() {
    MatchingEngine engine;
    MarketDataPublisher publisher{Clock::time_point{}};
    MarketDataPublisher replay{Clock::time_point{}};
    std::uint64_t trades = 0;
    std::uint64_t quantity = 0;
    for (std::uint64_t i = 1; i <= 1200; ++i) {
        const auto side = i % 2 == 0 ? Side::Buy : Side::Sell;
        const Order input = i % 7 == 0
            ? Order{OrderId{i}, side, OrderType::Market, Price{0}, Qty{1 + i % 13}}
            : limit(i, side, 95 + static_cast<std::int64_t>((i * 7) % 11),
                    i % 11 == 0 ? 0 : 1 + i % 13);
        const auto result = engine.submit(input);
        trades += result.fills.size();
        for (const auto& fill : result.fills) {
            quantity += fill.quantity.value();
        }
        const auto events = engine.drain_events();
        const auto time = Clock::time_point{} + std::chrono::milliseconds{i};
        publisher.publish(events, 25ns, time);
        replay.publish(events, 25ns, time);
        const auto snapshot = publisher.snapshot();
        compare(*snapshot, engine.book());
        require(snapshot->bids == replay.snapshot()->bids &&
                    snapshot->asks == replay.snapshot()->asks &&
                    snapshot->counters.trades == trades &&
                    snapshot->counters.traded_quantity == Qty{quantity},
                "replay depth deterministic and trade quantity counted once");
        require(!snapshot->spread_ticks || *snapshot->spread_ticks > 0,
                "completed matching batch never publishes a crossed book");
    }
    // Limit aggressor remainder rests; market remainder must disappear.
    MatchingEngine small;
    MarketDataPublisher projected;
    require(small.submit(limit(1, Side::Sell, 100, 2)).accepted, "small ask");
    projected.publish(small.drain_events(), 0ns);
    const auto buy = small.submit(limit(2, Side::Buy, 100, 5));
    require(buy.remaining == Qty{3}, "limit remainder");
    projected.publish(small.drain_events(), 0ns);
    compare(*projected.snapshot(), small.book());
    const auto sell = small.submit(Order{OrderId{3}, Side::Sell, OrderType::Market, Price{0}, Qty{10}});
    require(sell.remaining == Qty{7}, "market remainder");
    projected.publish(small.drain_events(), 0ns);
    compare(*projected.snapshot(), small.book());
    require(projected.snapshot()->order_count == 0, "market remainder never rests");
}

void signed_spreads_and_zero_elapsed() {
    for (const auto ask : {1LL, 100LL, std::numeric_limits<long long>::max()}) {
        MarketDataPublisher publisher{Clock::time_point{}};
        const std::vector<Event> events{
            OrderAcceptedEvent{OrderId{1}, Side::Buy, OrderType::Limit, Price{100}, Qty{1}},
            OrderAcceptedEvent{OrderId{2}, Side::Sell, OrderType::Limit, Price{ask}, Qty{1}}};
        publisher.publish(events, 0ns, Clock::time_point{});
        require(publisher.snapshot()->spread_ticks == ask - 100 &&
                    publisher.snapshot()->counters.operations_per_second() == 0.0,
                "locked/crossed/wide spread and zero elapsed are defined");
    }
}

void invalid_batches_are_atomic() {
    MarketDataPublisher publisher{Clock::time_point{}};
    const std::vector<Event> accepted{OrderAcceptedEvent{
        OrderId{1}, Side::Buy, OrderType::Limit, Price{100}, Qty{5}}};
    publisher.publish(accepted, 5ns, Clock::time_point{} + 1s);
    const auto before = publisher.snapshot();
    const auto fails = [&](const std::vector<Event>& events, std::chrono::nanoseconds latency,
                           Clock::time_point observed) {
        bool failed = false;
        try { publisher.publish(events, latency, observed); }
        catch (const std::invalid_argument&) { failed = true; }
        catch (const std::overflow_error&) { failed = true; }
        require(failed && publisher.snapshot() == before, "failed batch leaves snapshot unchanged");
    };
    fails(accepted, 0ns, Clock::time_point{} + 2s);
    fails({}, std::chrono::nanoseconds::max(), Clock::time_point{} + 2s);
    fails({}, -1ns, Clock::time_point{} + 2s);
    fails({}, 0ns, Clock::time_point{});
    fails({OrderReducedEvent{OrderId{1}, Qty{5}, Qty{6}}}, 0ns, Clock::time_point{} + 2s);
    // A later failure must also undo earlier successfully staged events.
    fails({OrderCancelledEvent{OrderId{1}}, OrderCancelledEvent{OrderId{2}}},
          0ns, Clock::time_point{} + 2s);
    fails({OrderAcceptedEvent{OrderId{2}, Side::Buy, OrderType::Limit, Price{100},
                             Qty{std::numeric_limits<std::uint64_t>::max()}}},
          0ns, Clock::time_point{} + 2s);
    const std::vector<Event> cancelled{OrderCancelledEvent{OrderId{1}}};
    publisher.publish(cancelled, 0ns, Clock::time_point{} + 2s);
    require(publisher.snapshot()->order_count == 0 && publisher.snapshot()->sequence == 2,
            "projection and time remain usable after failures");
}

void concurrent_readers_see_complete_snapshots() {
    MarketDataPublisher publisher{Clock::time_point{}};
    std::atomic<bool> done{false};
    std::atomic<std::uint64_t> reads{0};
    std::atomic<unsigned> ready{0};
    const auto reader = [&] {
        ready.fetch_add(1, std::memory_order_release);
        std::uint64_t previous = 0;
        do {
            const auto view = publisher.snapshot();
            require(view->sequence >= previous && view->sequence == view->counters.operations &&
                        view->order_count == view->sequence &&
                        view->counters.accepted_orders == view->sequence,
                    "reader sees a coherent monotonic publication");
            if (view->sequence != 0) {
                require(view->bids.size() == 1 && view->bids[0].quantity.value() == view->sequence &&
                            view->bids[0].order_count == view->sequence, "depth coherent with counters");
            }
            previous = view->sequence;
            reads.fetch_add(1, std::memory_order_relaxed);
        } while (!done.load(std::memory_order_acquire));
    };
    std::thread first{reader};
    std::thread second{reader};
    while (ready.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }
    for (std::uint64_t i = 1; i <= 500; ++i) {
        const std::vector<Event> events{OrderAcceptedEvent{
            OrderId{i}, Side::Buy, OrderType::Limit, Price{100}, Qty{1}}};
        publisher.publish(events, 1ns, Clock::time_point{} + std::chrono::milliseconds{i});
    }
    done.store(true, std::memory_order_release);
    first.join(); second.join();
    require(reads.load() > 0 && publisher.snapshot()->sequence == 500,
            "concurrent readers exercised final snapshot");
}
} // namespace

int main() {
    direct_book_events_and_counters();
    matching_batches_and_replay();
    signed_spreads_and_zero_elapsed();
    invalid_batches_are_atomic();
    concurrent_readers_see_complete_snapshots();
    std::cout << "All market-data tests passed\n";
}
