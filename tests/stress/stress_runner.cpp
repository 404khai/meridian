#include "stress_support.hpp"
#include "meridian/concurrency/concurrent_matching_engine.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <thread>

namespace {
using namespace stress;
using Clock = std::chrono::steady_clock;

struct Totals final {
    std::uint64_t operations{0}, accepted{0}, rejected{0}, trades{0}, events{0};
    std::uint64_t concurrent_operations{0}, concurrent_fills{0};
    std::uint64_t digest{14695981039346656037ULL};
    Histogram matching_latency, concurrent_latency;
    void hash(std::uint64_t value) { digest = (digest ^ value) * 1099511628211ULL; }
};

void compare_book(const Reference& reference, const OrderBook& book) {
    check(book.order_count() == reference.orders.size(), "resting order count mismatch");
    std::map<Price, std::vector<Order>> bids, asks;
    for (const auto& order : reference.orders) {
        const auto actual = book.find(order.id());
        check(actual && actual->side() == order.side() && actual->price() == order.price() &&
              actual->quantity() == order.quantity(), "resting order differs from reference");
        (order.side() == Side::Buy ? bids : asks)[order.price()].push_back(order);
    }
    check(book.level_count(Side::Buy) == bids.size() && book.level_count(Side::Sell) == asks.size(),
          "level count mismatch");
    for (const auto side : {Side::Buy, Side::Sell}) {
        for (const auto& [price, expected] : side == Side::Buy ? bids : asks) {
            const auto actual = book.orders_at(side, price);
            check(actual.size() == expected.size(), "FIFO size mismatch");
            std::uint64_t quantity = 0;
            for (std::size_t i = 0; i < actual.size(); ++i) {
                check(actual[i].id() == expected[i].id(), "FIFO priority mismatch");
                quantity += expected[i].quantity().value();
            }
            check(book.depth_at(side, price) == Qty{quantity}, "depth mismatch");
        }
    }
    check(book.best_bid() == (bids.empty() ? std::optional<Price>{} : bids.rbegin()->first) &&
          book.best_ask() == (asks.empty() ? std::optional<Price>{} : asks.begin()->first),
          "best price mismatch");
}

void matching_cycle(MatchingEngine& engine, Generator& generator, Totals& totals) {
    check(engine.book().empty(), "matching cycle starts with residual orders");
    Reference reference;
    const auto submit = [&](const Order& order) {
        const auto expected = reference.submit(order);
        const auto before = Clock::now();
        const auto actual = engine.submit(order);
        const auto after = Clock::now();
        totals.matching_latency.observe(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count()));
        const auto events = engine.drain_events();
        verify(order, expected, actual, events);
        check(engine.drain_events().empty(), "event drain repeats events");
        check(engine.book().order_count() == reference.orders.size(), "order count drift");
        if (engine.book().best_bid() && engine.book().best_ask()) {
            check(*engine.book().best_bid() < *engine.book().best_ask(), "crossed book");
        }
        ++totals.operations;
        totals.accepted += actual.accepted ? 1U : 0U;
        totals.rejected += actual.accepted ? 0U : 1U;
        totals.events += events.size();
        totals.trades += actual.fills.size();
        totals.hash(order.id().value());
        totals.hash(actual.accepted);
        totals.hash(actual.remaining.value());
        if (expected.rejection) { totals.hash(static_cast<std::uint64_t>(*expected.rejection)); }
        for (const auto& fill : actual.fills) {
            totals.hash(fill.resting_id.value()); totals.hash(fill.quantity.value());
            totals.hash(static_cast<std::uint64_t>(fill.price.value()));
        }
    };
    for (std::uint64_t i = 1; i <= 128; ++i) {
        submit(Order{OrderId{i}, i % 2 == 0 ? Side::Buy : Side::Sell, OrderType::Limit,
                     Price{i % 2 == 0 ? 90 : 110}, Qty{10}});
    }
    for (std::uint64_t i = 0; i < 2048; ++i) {
        auto id = 129 + i;
        auto side = generator.next() % 2 == 0 ? Side::Buy : Side::Sell;
        auto type = generator.next() % 5 == 0 ? OrderType::Market : OrderType::Limit;
        std::int64_t price = 95 + static_cast<std::int64_t>(generator.next() % 11);
        std::uint64_t quantity = 1 + generator.next() % 32;
        switch (i % 32) {
        case 0: quantity = 0; break;
        case 1: side = static_cast<Side>(99); break;
        case 2: type = static_cast<OrderType>(99); break;
        case 3: type = OrderType::Limit; price = -1; break;
        case 4:
            if (!reference.orders.empty()) { id = reference.orders.front().id().value(); }
            break;
        default: break;
        }
        if (reference.orders.size() >= 256) {
            id = 129 + i;
            side = reference.orders.front().side() == Side::Buy ? Side::Sell : Side::Buy;
            type = OrderType::Market;
            quantity = std::numeric_limits<std::uint64_t>::max();
        }
        submit(Order{OrderId{id}, side, type, Price{price}, Qty{quantity}});
        if (i % 32 == 0) { compare_book(reference, engine.book()); }
    }
    compare_book(reference, engine.book());
    for (const auto side : {Side::Buy, Side::Sell}) {
        submit(Order{OrderId{side == Side::Buy ? 5000U : 5001U}, side, OrderType::Market,
                     Price{0}, Qty{std::numeric_limits<std::uint64_t>::max()}});
    }
    check(engine.book().empty() && reference.orders.empty(), "cycle failed to drain");
}

void concurrent_cycle(Totals& totals) {
    ConcurrentMatchingEngine engine;
    constexpr std::size_t producers = 4, per_producer = 64;
    constexpr std::size_t makers = producers * per_producer;
    for (std::size_t i = 1; i <= makers; ++i) {
        check(engine.submit(Order{OrderId{i}, Side::Sell, OrderType::Limit, Price{100}, Qty{1}})
                  .get().accepted, "concurrent fixture rejected");
    }
    struct Producer final {
        Histogram latency;
        std::vector<std::uint64_t> makers;
        std::exception_ptr failure;
    };
    std::array<Producer, producers> results;
    std::vector<std::thread> workers;
    std::atomic<bool> started{false};
    // Ensure thread creation failure cannot destroy a joinable std::thread.
    try {
        for (std::size_t p = 0; p < producers; ++p) {
            workers.emplace_back([&, p] {
                try {
                    while (!started.load(std::memory_order_acquire)) { std::this_thread::yield(); }
                    for (std::size_t i = 0; i < per_producer; ++i) {
                        const auto id = makers + p * per_producer + i + 1;
                        const auto before = Clock::now();
                        const auto result = engine.submit(Order{
                            OrderId{id}, Side::Buy, OrderType::Market, Price{0}, Qty{1}}).get();
                        const auto after = Clock::now();
                        results[p].latency.observe(static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count()));
                        check(result.accepted && result.remaining == Qty{0} && result.fills.size() == 1,
                              "concurrent completion missing/incorrect");
                        const auto& fill = result.fills[0];
                        check(fill.aggressor_id == OrderId{id} && fill.price == Price{100} &&
                              fill.quantity == Qty{1}, "incorrect concurrent fill");
                        results[p].makers.push_back(fill.resting_id.value());
                    }
                } catch (...) { results[p].failure = std::current_exception(); }
            });
        }
    } catch (...) {
        started.store(true, std::memory_order_release);
        for (auto& worker : workers) { worker.join(); }
        throw;
    }
    started.store(true, std::memory_order_release);
    for (auto& worker : workers) { worker.join(); }
    std::array<bool, makers> seen{};
    for (const auto& result : results) {
        if (result.failure) { std::rethrow_exception(result.failure); }
        totals.concurrent_latency.merge(result.latency);
        for (const auto id : result.makers) {
            check(id > 0 && id <= makers && !seen[id - 1], "concurrent maker missing/duplicated");
            seen[id - 1] = true;
        }
    }
    check(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }), "missing maker");
    const auto snapshot = engine.snapshot();
    check(snapshot->processed_submissions == 2 * makers && snapshot->order_count == 0 &&
          !snapshot->best_bid && !snapshot->best_ask, "concurrent snapshot/accounting drift");
    totals.concurrent_operations += 2 * makers;
    totals.concurrent_fills += makers;
    // Adapter event log has no public feed; event identity checks apply to the
    // direct matcher. Recreate the adapter per cycle to bound retained history.
}

void self_test() {
    Histogram histogram;
    for (std::uint64_t i = 1; i <= 100; ++i) { histogram.observe(i); }
    check(histogram.percentile(50, 100) == 50 && histogram.percentile(99, 100) == 99 &&
          histogram.maximum == 100, "histogram nearest rank failed");
    Histogram tail;
    tail.observe(1024); histogram.merge(tail);
    check(histogram.percentile(100, 100) == 1039 && histogram.count == 101,
          "histogram upper-bound/merge failed");
    MatchingEngine engine;
    Reference reference;
    const Order input{OrderId{1}, Side::Buy, OrderType::Limit, Price{100}, Qty{1}};
    const auto expected = reference.submit(input);
    const auto result = engine.submit(input);
    const auto events = engine.drain_events();
    verify(input, expected, result, events);
    auto corrupted = events;
    corrupted.push_back(events[0]);
    bool caught = false;
    try { verify(input, expected, result, corrupted); } catch (const std::runtime_error&) { caught = true; }
    check(caught, "duplicate-event injection escaped checker");
    caught = false;
    try { verify(input, expected, result, {}); } catch (const std::runtime_error&) { caught = true; }
    check(caught, "dropped-event injection escaped checker");
    const Order market{OrderId{2}, Side::Sell, OrderType::Market, Price{0}, Qty{1}};
    const auto expected_fill = reference.submit(market);
    const auto actual_fill = engine.submit(market);
    corrupted = engine.drain_events();
    std::get<TradeGeneratedEvent>(corrupted[2]).trade.price = Price{101};
    caught = false;
    try { verify(market, expected_fill, actual_fill, corrupted); }
    catch (const std::runtime_error&) { caught = true; }
    check(caught, "corrupted-trade injection escaped checker");
    Generator first{42}, second{42};
    Totals a, b;
    MatchingEngine first_engine, second_engine;
    matching_cycle(first_engine, first, a); matching_cycle(second_engine, second, b);
    check(a.digest == b.digest && a.events == b.events && a.trades == b.trades,
          "seeded stress replay diverged");
}

template <typename T>
T integer(std::string_view value) {
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    check(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && result > 0,
          "expected a positive integer");
    return result;
}

void print_histogram(const Histogram& h) {
    std::cout << "{\"samples\":" << h.count << ",\"p50_ns_upper_bound\":" << h.percentile(50, 100)
        << ",\"p95_ns_upper_bound\":" << h.percentile(95, 100)
        << ",\"p99_ns_upper_bound\":" << h.percentile(99, 100)
        << ",\"p999_ns_upper_bound\":" << h.percentile(999, 1000)
        << ",\"max_ns\":" << h.maximum << '}';
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::uint64_t cycles = 4, seconds = 0, seed = 42;
        bool fixed_cycles = false;
        std::string mode = "both", revision = "unknown";
        for (int i = 1; i < argc; ++i) {
            const std::string_view option{argv[i]};
            if (option == "--self-test") { self_test(); std::cout << "stress self-tests passed\n"; return 0; }
            check(i + 1 < argc, "missing option value");
            const std::string_view value{argv[++i]};
            if (option == "--cycles") { cycles = integer<std::uint64_t>(value); fixed_cycles = true; }
            else if (option == "--seconds") { seconds = integer<std::uint64_t>(value); }
            else if (option == "--seed") { seed = integer<std::uint64_t>(value); }
            else if (option == "--mode") { mode = value; }
            else if (option == "--revision") {
                check(value.size() >= 7 && value.size() <= 40 &&
                      value.find_first_not_of("0123456789abcdef") == std::string_view::npos, "invalid revision");
                revision = value;
            } else { throw std::runtime_error{"unknown option"}; }
        }
        check(!(fixed_cycles && seconds != 0) && seconds <= 86400 && cycles <= 1000000000,
              "choose --cycles or --seconds (maximum one day)");
        check(mode == "matching" || mode == "concurrent" || mode == "both", "unknown mode");
        Generator generator{seed};
        MatchingEngine engine;
        Totals totals;
        const auto started = Clock::now();
        std::uint64_t completed = 0;
        while (seconds != 0 ? Clock::now() - started < std::chrono::seconds{seconds} : completed < cycles) {
            try {
                if (mode != "concurrent") { matching_cycle(engine, generator, totals); }
                if (mode != "matching") { concurrent_cycle(totals); }
            } catch (const std::exception& error) {
                throw std::runtime_error{"seed=" + std::to_string(seed) + " cycle=" +
                    std::to_string(completed + 1) + ": " + error.what()};
            }
            ++completed;
        }
        const auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
        std::cout << "{\"mode\":\"" << mode << "\",\"revision\":\"" << revision
            << "\",\"profile\":\"" << MERIDIAN_STRESS_PROFILE << "\",\"seed\":" << seed
            << ",\"cycles\":" << completed << ",\"elapsed_seconds\":" << elapsed
            << ",\"verified_operations_per_second\":"
            << static_cast<double>(totals.operations + totals.concurrent_operations) / elapsed
            << ",\"matching_operations\":" << totals.operations << ",\"accepted\":" << totals.accepted
            << ",\"rejected\":" << totals.rejected << ",\"trades\":" << totals.trades
            << ",\"verified_events\":" << totals.events << ",\"checksum\":" << totals.digest
            << ",\"concurrent_operations\":" << totals.concurrent_operations
            << ",\"concurrent_fills\":" << totals.concurrent_fills << ",\"matching_latency\":";
        print_histogram(totals.matching_latency);
        std::cout << ",\"concurrent_completion_latency\":";
        print_histogram(totals.concurrent_latency);
        std::cout << "}\n";
    } catch (const std::exception& error) {
        std::cerr << "stress failure: " << error.what() << '\n';
        return 1;
    }
}
