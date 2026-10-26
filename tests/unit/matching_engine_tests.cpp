#include "meridian/engine/matching_engine.hpp"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

using meridian::Fill;
using meridian::MatchingEngine;
using meridian::Order;
using meridian::OrderId;
using meridian::OrderType;
using meridian::Price;
using meridian::Qty;
using meridian::Side;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

Order order(std::uint64_t id, Side side, OrderType type, std::int64_t price,
            std::uint64_t quantity) {
    return Order{OrderId{id}, side, type, Price{price}, Qty{quantity}};
}

void matches_best_price_before_worse_prices() {
    MatchingEngine engine;
    require(engine.submit(order(1, Side::Sell, OrderType::Limit, 101, 3)).accepted,
            "first ask accepted");
    require(engine.submit(order(2, Side::Sell, OrderType::Limit, 100, 4)).accepted,
            "better ask accepted");
    require(engine.submit(order(3, Side::Sell, OrderType::Limit, 102, 5)).accepted,
            "worse ask accepted");

    const auto result = engine.submit(order(10, Side::Buy, OrderType::Limit, 101, 6));
    require(result.accepted, "aggressive buy accepted");
    require(result.fills.size() == 2, "buy fills two price levels");
    require(result.fills[0].resting_id == OrderId{2}, "best ask fills first");
    require(result.fills[0].price == Price{100}, "first fill uses maker price");
    require(result.fills[0].quantity == Qty{4}, "best ask fills fully");
    require(result.fills[1].resting_id == OrderId{1}, "next ask fills second");
    require(result.fills[1].quantity == Qty{2}, "second ask is partially filled");
    require(result.remaining == Qty{0}, "aggressive buy is fully filled");
    require(!engine.book().contains(OrderId{2}), "fully filled ask is removed");
    require(engine.book().find(OrderId{1})->quantity() == Qty{1},
            "partial ask retains remaining quantity");
}

void preserves_fifo_priority_within_a_price() {
    MatchingEngine engine;
    require(engine.submit(order(20, Side::Sell, OrderType::Limit, 100, 2)).accepted,
            "first ask accepted");
    require(engine.submit(order(21, Side::Sell, OrderType::Limit, 100, 3)).accepted,
            "second ask accepted");

    const auto result = engine.submit(order(22, Side::Buy, OrderType::Limit, 100, 4));
    require(result.fills.size() == 2, "buy consumes both FIFO asks");
    require(result.fills[0].resting_id == OrderId{20}, "oldest ask fills first");
    require(result.fills[1].resting_id == OrderId{21}, "newer ask fills second");
    require(result.fills[0].quantity == Qty{2}, "oldest ask fills fully");
    require(result.fills[1].quantity == Qty{2}, "newer ask is partially filled");
    require(engine.book().orders_at(Side::Sell, Price{100}).front().id() == OrderId{21},
            "partial fill preserves remaining time priority");
}

void rests_limit_remainder_and_does_not_cross() {
    MatchingEngine engine;
    require(engine.submit(order(30, Side::Sell, OrderType::Limit, 105, 3)).accepted,
            "ask accepted");

    const auto result = engine.submit(order(31, Side::Buy, OrderType::Limit, 104, 5));
    require(result.accepted, "non-crossing limit accepted");
    require(result.fills.empty(), "non-crossing limit has no fills");
    require(result.remaining == Qty{5}, "entire non-crossing order remains");
    require(engine.book().best_bid() == std::optional<Price>{Price{104}},
            "limit remainder rests at bid price");
    require(engine.book().best_ask() == std::optional<Price>{Price{105}},
            "non-crossing ask remains untouched");
}

void market_orders_consume_liquidity_without_resting() {
    MatchingEngine engine;
    require(engine.submit(order(40, Side::Sell, OrderType::Limit, 100, 2)).accepted,
            "ask accepted");

    const auto result = engine.submit(order(41, Side::Buy, OrderType::Market, 0, 5));
    require(result.accepted, "market order accepted");
    require(result.fills.size() == 1, "market order consumes available liquidity");
    require(result.fills.front().quantity == Qty{2}, "market order fills available quantity");
    require(result.remaining == Qty{3}, "unfilled market quantity is reported");
    require(engine.book().empty(), "market remainder does not rest");
}

void rejects_invalid_orders_without_mutating_book() {
    MatchingEngine engine;
    const auto zero_quantity = engine.submit(order(50, Side::Buy, OrderType::Limit, 100, 0));
    require(!zero_quantity.accepted, "zero quantity is rejected");
    const auto invalid_price = engine.submit(order(51, Side::Buy, OrderType::Limit, 0, 1));
    require(!invalid_price.accepted, "non-positive limit price is rejected");
    require(engine.book().empty(), "rejected orders do not mutate book");
}

void rejects_duplicate_ids_before_matching() {
    MatchingEngine engine;
    require(engine.submit(order(60, Side::Sell, OrderType::Limit, 100, 2)).accepted,
            "resting order accepted");

    const auto duplicate = engine.submit(order(60, Side::Buy, OrderType::Limit, 100, 1));
    require(!duplicate.accepted, "duplicate ID is rejected");
    require(duplicate.fills.empty(), "duplicate rejection has no fills");
    require(engine.book().find(OrderId{60})->side() == Side::Sell,
            "duplicate rejection leaves original order intact");
    require(engine.book().find(OrderId{60})->quantity() == Qty{2},
            "duplicate rejection leaves original quantity intact");
}

void large_sweeps_preserve_fifo_and_events() {
    for (const Side resting_side : {Side::Buy, Side::Sell}) {
        MatchingEngine engine;
        constexpr std::uint64_t count = 256;
        for (std::uint64_t id = 1; id <= count; ++id) {
            require(engine.submit(order(id, resting_side, OrderType::Limit, 100, 2)).accepted,
                    "sweep fixture accepted");
        }
        (void)engine.drain_events();
        const Side aggressor_side = resting_side == Side::Buy ? Side::Sell : Side::Buy;
        const auto result = engine.submit(
            order(count + 1, aggressor_side, OrderType::Market, 0, 2 * count - 1));
        require(result.accepted && result.remaining == Qty{0}, "large sweep completes");
        require(result.fills.size() == count, "each FIFO maker produces a fill");
        const auto events = engine.drain_events();
        require(events.size() == 1 + 2 * count, "accepted then matched/trade event pairs");
        require(std::get<meridian::OrderAcceptedEvent>(events[0]).id == OrderId{count + 1},
                "aggressor accepted first");
        for (std::uint64_t index = 0; index < count; ++index) {
            const Qty quantity{index + 1 == count ? 1U : 2U};
            const auto& fill = result.fills[index];
            const auto& matched = std::get<meridian::OrderMatchedEvent>(events[1 + 2 * index]);
            const auto& trade = std::get<meridian::TradeGeneratedEvent>(events[2 + 2 * index]).trade;
            require(fill.resting_id == OrderId{index + 1} && fill.price == Price{100} &&
                        fill.quantity == quantity && fill.aggressor_id == OrderId{count + 1},
                    "large sweep fills retain FIFO maker values");
            require(matched.resting_id == fill.resting_id && matched.price == fill.price &&
                        matched.quantity == fill.quantity && matched.aggressor_id == fill.aggressor_id,
                    "match event retains maker values after node removal");
            require(trade.resting_id == fill.resting_id && trade.price == fill.price &&
                        trade.quantity == fill.quantity && trade.aggressor_id == fill.aggressor_id,
                    "trade event retains maker values after node removal");
        }
        require(engine.book().order_count() == 1 &&
                    engine.book().find(OrderId{count})->quantity() == Qty{1},
                "partially filled final maker remains");
        const auto final_fill = engine.submit(
            order(count + 2, aggressor_side, OrderType::Market, 0, 1));
        require(final_fill.fills.size() == 1 &&
                    final_fill.fills[0].resting_id == OrderId{count} && engine.book().empty(),
                "final fill removes the last price level");
        const auto empty = engine.submit(
            order(count + 3, aggressor_side, OrderType::Market, 0, 1));
        require(empty.accepted && empty.fills.empty() && empty.remaining == Qty{1},
                "empty opposing side terminates matching");
    }
}

} // namespace

int main() {
    large_sweeps_preserve_fifo_and_events();
    matches_best_price_before_worse_prices();
    preserves_fifo_priority_within_a_price();
    rests_limit_remainder_and_does_not_cross();
    market_orders_consume_liquidity_without_resting();
    rejects_invalid_orders_without_mutating_book();
    rejects_duplicate_ids_before_matching();
    std::cout << "All Meridian Phase 2 tests passed\n";
    return EXIT_SUCCESS;
}
