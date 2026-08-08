#include "meridian/engine/matching_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using meridian::Fill;
using meridian::MatchingEngine;
using meridian::Order;
using meridian::OrderBook;
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

Order make_order(std::uint64_t id, Side side, OrderType type, std::int64_t price,
                 std::uint64_t quantity) {
    return Order{OrderId{id}, side, type, Price{price}, Qty{quantity}};
}

void require_fill_equal(const Fill& left, const Fill& right) {
    require(left.aggressor_id == right.aggressor_id, "deterministic fill aggressor ID");
    require(left.resting_id == right.resting_id, "deterministic fill resting ID");
    require(left.price == right.price, "deterministic fill price");
    require(left.quantity == right.quantity, "deterministic fill quantity");
}

void edge_case_matrix() {
    MatchingEngine engine;
    require(!engine.book().best_bid().has_value(), "empty book has no bid");
    require(!engine.book().best_ask().has_value(), "empty book has no ask");

    const auto empty_market = engine.submit(
        make_order(1, Side::Buy, OrderType::Market, 0, 7));
    require(empty_market.accepted, "empty-book market order is accepted");
    require(empty_market.fills.empty(), "empty-book market order has no fills");
    require(empty_market.remaining == Qty{7}, "empty-book market remainder is reported");
    require(engine.book().empty(), "empty-book market order does not rest");

    require(engine.submit(make_order(2, Side::Sell, OrderType::Limit, 105, 4)).accepted,
            "ask enters book");
    require(engine.submit(make_order(3, Side::Buy, OrderType::Limit, 104, 3)).accepted,
            "non-crossing bid enters book");
    require(engine.book().best_bid() == std::optional<Price>{Price{104}},
            "best bid is selected correctly");
    require(engine.book().best_ask() == std::optional<Price>{Price{105}},
            "best ask is selected correctly");

    const auto partial = engine.submit(make_order(4, Side::Buy, OrderType::Limit, 105, 6));
    require(partial.accepted && partial.fills.size() == 1, "crossing order fills one level");
    require(partial.fills.front().quantity == Qty{4}, "partial matrix fill quantity");
    require(partial.remaining == Qty{2}, "partial matrix remainder quantity");
    require(engine.book().find(OrderId{4})->quantity() == Qty{2},
            "limit remainder is resting");

    OrderBook operations;
    require(operations.insert(make_order(40, Side::Buy, OrderType::Limit, 103, 2)),
            "operation matrix order inserts");
    require(operations.modify(OrderId{40}, Price{103}, Qty{1}),
            "resting order can be modified");
    require(operations.cancel(OrderId{40}), "modified order can be cancelled");
    require(!operations.cancel(OrderId{40}), "repeated cancellation is rejected");

    const auto zero_quantity = engine.submit(
        make_order(5, Side::Buy, OrderType::Limit, 100, 0));
    require(!zero_quantity.accepted, "zero quantity is rejected");
    const auto invalid_price = engine.submit(
        make_order(6, Side::Buy, OrderType::Limit, -1, 1));
    require(!invalid_price.accepted, "negative price is rejected");
    const auto invalid_side = engine.submit(
        make_order(7, static_cast<Side>(99), OrderType::Limit, 100, 1));
    require(!invalid_side.accepted, "invalid side is rejected");
    const auto invalid_type = engine.submit(
        make_order(8, Side::Buy, static_cast<OrderType>(99), 100, 1));
    require(!invalid_type.accepted, "invalid order type is rejected");
}

struct LedgerEntry final {
    std::uint64_t submitted{0};
    std::uint64_t maker_filled{0};
    std::uint64_t taker_filled{0};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price price{1};
};

struct ReplaySnapshot final {
    std::vector<SubmitResult> results;
    std::vector<std::optional<Order>> orders;
    std::optional<Price> best_bid;
    std::optional<Price> best_ask;
    std::size_t order_count{0};
};

class DeterministicGenerator final {
public:
    explicit DeterministicGenerator(std::uint64_t seed) : state_(seed) {}

    [[nodiscard]] std::uint64_t next() noexcept {
        state_ ^= state_ << 7U;
        state_ ^= state_ >> 9U;
        state_ ^= state_ << 8U;
        return state_;
    }

private:
    std::uint64_t state_;
};

std::vector<Order> generated_orders() {
    DeterministicGenerator generator{0x6d6572696469616eULL};
    std::vector<Order> orders;
    orders.reserve(1500);
    for (std::uint64_t index = 0; index < 1500; ++index) {
        const Side side = (generator.next() & 1U) == 0U ? Side::Buy : Side::Sell;
        const bool market = generator.next() % 8U == 0U;
        const auto price = static_cast<std::int64_t>(95U + generator.next() % 11U);
        const auto quantity = 1U + generator.next() % 20U;
        orders.push_back(make_order(1000U + index, side,
                                    market ? OrderType::Market : OrderType::Limit,
                                    market ? 0 : price, quantity));
    }
    return orders;
}

ReplaySnapshot run_property_sequence(const std::vector<Order>& orders) {
    MatchingEngine engine;
    std::unordered_map<OrderId, LedgerEntry> ledger;
    ReplaySnapshot snapshot;
    snapshot.results.reserve(orders.size());
    snapshot.orders.reserve(orders.size());

    for (const auto& order : orders) {
        ledger.emplace(order.id(), LedgerEntry{
            order.quantity().value(),
            0,
            0,
            order.side(),
            order.type(),
            order.price(),
        });
        const auto result = engine.submit(order);
        require(result.accepted, "generated valid order is accepted");

        std::uint64_t taker_filled = 0;
        for (const auto& fill : result.fills) {
            require(fill.quantity.value() > 0, "fill quantity is positive");
            require(fill.price.value() > 0, "fill price is positive");
            require(fill.aggressor_id == order.id(), "fill aggressor matches submission");
            auto resting = ledger.find(fill.resting_id);
            require(resting != ledger.end(), "fill references a known resting order");
            resting->second.maker_filled += fill.quantity.value();
            taker_filled += fill.quantity.value();
        }
        ledger.at(order.id()).taker_filled += taker_filled;
        require(taker_filled + result.remaining.value() == order.quantity().value(),
                "taker quantity is conserved per submission");

        if (order.type() == OrderType::Market) {
            require(!engine.book().contains(order.id()), "market order never rests");
        } else if (result.remaining.value() > 0) {
            const auto resting = engine.book().find(order.id());
            require(resting.has_value(), "limit remainder rests in book");
            require(resting->quantity() == result.remaining,
                    "resting limit quantity equals reported remainder");
        }
        snapshot.results.push_back(result);

        const auto bid = engine.book().best_bid();
        const auto ask = engine.book().best_ask();
        if (bid.has_value() && ask.has_value()) {
            require(*bid < *ask, "book never remains crossed");
        }
    }

    std::map<std::int64_t, std::uint64_t> expected_bids;
    std::map<std::int64_t, std::uint64_t> expected_asks;
    for (const auto& [id, entry] : ledger) {
        const auto resting = engine.book().find(id);
        const std::uint64_t resting_quantity = resting.has_value() ? resting->quantity().value() : 0;
        require(entry.maker_filled + entry.taker_filled + resting_quantity == entry.submitted,
                "total quantity is conserved per order");
        if (resting.has_value()) {
            auto& expected = resting->side() == Side::Buy ? expected_bids : expected_asks;
            expected[resting->price().value()] += resting->quantity().value();
        }
    }

    for (const auto& [price, quantity] : expected_bids) {
        require(engine.book().depth_at(Side::Buy, Price{price}) == std::optional<Qty>{Qty{quantity}},
                "bid depth matches order ledger");
    }
    for (const auto& [price, quantity] : expected_asks) {
        require(engine.book().depth_at(Side::Sell, Price{price}) == std::optional<Qty>{Qty{quantity}},
                "ask depth matches order ledger");
    }

    for (const auto& order : orders) {
        snapshot.orders.push_back(engine.book().find(order.id()));
    }
    snapshot.best_bid = engine.book().best_bid();
    snapshot.best_ask = engine.book().best_ask();
    snapshot.order_count = engine.book().order_count();
    return snapshot;
}

void compare_snapshots(const ReplaySnapshot& left, const ReplaySnapshot& right) {
    require(left.results.size() == right.results.size(), "replays have equal result counts");
    for (std::size_t index = 0; index < left.results.size(); ++index) {
        require(left.results[index].accepted == right.results[index].accepted,
                "replays agree on acceptance");
        require(left.results[index].remaining == right.results[index].remaining,
                "replays agree on remainder");
        require(left.results[index].fills.size() == right.results[index].fills.size(),
                "replays have equal fill counts");
        for (std::size_t fill_index = 0; fill_index < left.results[index].fills.size(); ++fill_index) {
            require_fill_equal(left.results[index].fills[fill_index],
                               right.results[index].fills[fill_index]);
        }
    }

    require(left.best_bid == right.best_bid, "replays agree on best bid");
    require(left.best_ask == right.best_ask, "replays agree on best ask");
    require(left.order_count == right.order_count, "replays agree on order count");
    require(left.orders.size() == right.orders.size(), "replays have equal order snapshots");
    for (std::size_t index = 0; index < left.orders.size(); ++index) {
        require(left.orders[index].has_value() == right.orders[index].has_value(),
                "replays agree on order presence");
        if (left.orders[index].has_value()) {
            require(left.orders[index]->id() == right.orders[index]->id(),
                    "replays agree on order ID");
            require(left.orders[index]->price() == right.orders[index]->price(),
                    "replays agree on order price");
            require(left.orders[index]->quantity() == right.orders[index]->quantity(),
                    "replays agree on order quantity");
            require(left.orders[index]->sequence() == right.orders[index]->sequence(),
                    "replays agree on order sequence");
        }
    }
}

void randomized_invariants_are_repeatable() {
    const auto orders = generated_orders();
    const auto first = run_property_sequence(orders);
    const auto second = run_property_sequence(orders);
    compare_snapshots(first, second);
}

void large_synthetic_sequence_stays_consistent() {
    MatchingEngine engine;
    OrderBook stress_book;
    constexpr std::uint64_t resting_orders = 10000;
    for (std::uint64_t index = 0; index < resting_orders; ++index) {
        const bool bid = index % 2U == 0U;
        const auto stress_order = make_order(
            50000U + index,
            bid ? Side::Buy : Side::Sell,
            OrderType::Limit,
            bid ? 90 + static_cast<std::int64_t>(index % 10U)
                : 110 + static_cast<std::int64_t>(index % 10U),
            1U + index % 7U);
        require(stress_book.insert(stress_order), "non-crossing stress order rests");
    }
    require(stress_book.order_count() == resting_orders, "stress inserts all orders");
    require(stress_book.best_bid() == std::optional<Price>{Price{98}},
            "stress best bid is correct");
    require(stress_book.best_ask() == std::optional<Price>{Price{111}},
            "stress best ask is correct");

    std::size_t cancelled = 0;
    for (std::uint64_t index = 0; index < resting_orders; index += 3U) {
        require(stress_book.cancel(OrderId{50000U + index}), "stress cancellation succeeds");
        ++cancelled;
    }
    require(stress_book.order_count() == resting_orders - cancelled,
            "stress cancellations update order count");

    MatchingEngine matching_engine;
    constexpr std::uint64_t matched_orders = 2000;
    for (std::uint64_t index = 0; index < matched_orders; ++index) {
        require(matching_engine.submit(make_order(
            70000U + index, Side::Sell, OrderType::Limit, 100, 1)).accepted,
                "matching stress ask is accepted");
    }
    const auto market_result = matching_engine.submit(
        make_order(80000, Side::Buy, OrderType::Market, 0, matched_orders));
    require(market_result.accepted, "matching stress market order is accepted");
    require(market_result.fills.size() == matched_orders,
            "matching stress fills every resting order");
    require(market_result.remaining == Qty{0}, "matching stress has no remainder");
    require(matching_engine.book().empty(), "matching stress fully drains the book");
}

} // namespace

int main() {
    edge_case_matrix();
    randomized_invariants_are_repeatable();
    large_synthetic_sequence_stays_consistent();
    std::cout << "All Meridian Phase 5 comprehensive tests passed\n";
    return EXIT_SUCCESS;
}
