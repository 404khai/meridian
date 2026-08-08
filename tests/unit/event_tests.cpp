#include "meridian/book/order_book.hpp"
#include "meridian/engine/matching_engine.hpp"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <variant>

namespace {

using meridian::Event;
using meridian::MatchingEngine;
using meridian::Order;
using meridian::OrderAcceptedEvent;
using meridian::OrderBook;
using meridian::OrderCancelledEvent;
using meridian::OrderId;
using meridian::OrderMatchedEvent;
using meridian::OrderModifiedEvent;
using meridian::OrderRejectedEvent;
using meridian::OrderReducedEvent;
using meridian::OrderType;
using meridian::Price;
using meridian::Qty;
using meridian::Side;
using meridian::TradeGeneratedEvent;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

Order limit_order(std::uint64_t id, Side side, std::int64_t price, std::uint64_t quantity) {
    return Order{OrderId{id}, side, OrderType::Limit, Price{price}, Qty{quantity}};
}

void book_operations_emit_typed_events() {
    OrderBook book;
    require(book.insert(limit_order(1, Side::Buy, 100, 5)), "order inserts");
    require(book.events().size() == 1, "acceptance is logged");
    require(std::holds_alternative<OrderAcceptedEvent>(book.events().at(0)),
            "acceptance has the expected type");
    const auto& accepted = std::get<OrderAcceptedEvent>(book.events().at(0));
    require(accepted.id == OrderId{1} && accepted.quantity == Qty{5},
            "acceptance contains order details");

    require(!book.insert(limit_order(1, Side::Sell, 101, 1)), "duplicate is rejected");
    require(std::holds_alternative<OrderRejectedEvent>(book.events().at(1)),
            "rejection has the expected type");
    require(std::get<OrderRejectedEvent>(book.events().at(1)).reason ==
                meridian::RejectReason::DuplicateOrderId,
            "rejection contains its reason");

    require(book.modify(OrderId{1}, Price{100}, Qty{3}), "order modifies");
    require(std::holds_alternative<OrderModifiedEvent>(book.events().at(2)),
            "modification has the expected type");
    require(std::get<OrderModifiedEvent>(book.events().at(2)).priority_preserved,
            "quantity decrease records preserved priority");

    require(book.reduce_quantity(OrderId{1}, Qty{1}), "order reduces");
    require(std::holds_alternative<OrderReducedEvent>(book.events().at(3)),
            "reduction has the expected type");
    require(std::get<OrderReducedEvent>(book.events().at(3)).new_quantity == Qty{2},
            "reduction records remaining quantity");

    require(book.cancel(OrderId{1}), "order cancels");
    require(std::holds_alternative<OrderCancelledEvent>(book.events().at(4)),
            "cancellation has the expected type");
    require(std::get<OrderCancelledEvent>(book.events().at(4)).id == OrderId{1},
            "cancellation contains order ID");
}

void matching_emits_ordered_match_and_trade_events() {
    MatchingEngine engine;
    require(engine.submit(limit_order(10, Side::Sell, 100, 4)).accepted,
            "resting ask is accepted");
    (void)engine.drain_events();

    const auto result = engine.submit(limit_order(11, Side::Buy, 100, 2));
    require(result.accepted && result.fills.size() == 1, "aggressive order matches");
    const auto events = engine.drain_events();
    require(events.size() == 3, "submission has acceptance, match, and trade events");
    require(std::holds_alternative<OrderAcceptedEvent>(events[0]),
            "acceptance precedes matching");
    require(std::holds_alternative<OrderMatchedEvent>(events[1]),
            "match event follows acceptance");
    require(std::holds_alternative<TradeGeneratedEvent>(events[2]),
            "trade event follows match");

    const auto& matched = std::get<OrderMatchedEvent>(events[1]);
    require(matched.aggressor_id == OrderId{11} && matched.resting_id == OrderId{10},
            "match identifies both orders");
    require(matched.price == Price{100} && matched.quantity == Qty{2},
            "match contains execution terms");
    const auto& trade = std::get<TradeGeneratedEvent>(events[2]).trade;
    require(trade.aggressor_id == matched.aggressor_id && trade.resting_id == matched.resting_id,
            "trade mirrors match participants");
    require(trade.price == matched.price && trade.quantity == matched.quantity,
            "trade mirrors match terms");
}

void drain_is_queryable_and_consuming() {
    OrderBook book;
    require(book.insert(limit_order(20, Side::Buy, 101, 1)), "order inserts");
    require(book.events().size() == 1, "event is queryable before drain");
    const auto drained = book.drain_events();
    require(drained.size() == 1, "drain returns all events");
    require(book.events().empty(), "drain consumes the log");
}

} // namespace

int main() {
    book_operations_emit_typed_events();
    matching_emits_ordered_match_and_trade_events();
    drain_is_queryable_and_consuming();
    std::cout << "All Meridian Phase 4 tests passed\n";
    return EXIT_SUCCESS;
}
