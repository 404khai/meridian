#include "meridian/book/order_book.hpp"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using meridian::Order;
using meridian::OrderBook;
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

Order limit_order(std::uint64_t id, Side side, std::int64_t price, std::uint64_t quantity) {
    return Order{OrderId{id}, side, OrderType::Limit, Price{price}, Qty{quantity}};
}

void empty_book_queries() {
    OrderBook book;
    require(book.empty(), "new book is empty");
    require(book.order_count() == 0, "new book has no orders");
    require(book.level_count(Side::Buy) == 0, "new book has no bid levels");
    require(book.level_count(Side::Sell) == 0, "new book has no ask levels");
    require(!book.best_bid().has_value(), "empty book has no best bid");
    require(!book.best_ask().has_value(), "empty book has no best ask");
    require(!book.depth_at(Side::Buy, Price{100}).has_value(), "missing depth is absent");
    require(book.orders_at(Side::Sell, Price{100}).empty(), "missing level has no orders");
    require(!book.find(OrderId{1}).has_value(), "missing order is absent");
    require(!book.cancel(OrderId{1}), "cancelling missing order fails");
}

void insert_and_query_price_levels() {
    OrderBook book;
    require(book.insert(limit_order(1, Side::Buy, 100, 4)), "first bid inserts");
    require(book.insert(limit_order(2, Side::Buy, 101, 6)), "second bid inserts");
    require(book.insert(limit_order(3, Side::Sell, 105, 3)), "first ask inserts");
    require(book.insert(limit_order(4, Side::Sell, 104, 7)), "second ask inserts");

    require(book.best_bid() == std::optional<Price>{Price{101}}, "best bid is highest bid");
    require(book.best_ask() == std::optional<Price>{Price{104}}, "best ask is lowest ask");
    require(book.depth_at(Side::Buy, Price{100}) == std::optional<Qty>{Qty{4}},
            "bid depth is tracked");
    require(book.depth_at(Side::Sell, Price{104}) == std::optional<Qty>{Qty{7}},
            "ask depth is tracked");
    require(book.level_count(Side::Buy) == 2, "two bid levels exist");
    require(book.level_count(Side::Sell) == 2, "two ask levels exist");
    require(book.order_count() == 4, "all orders are counted");
}

void preserve_fifo_order_at_a_level() {
    OrderBook book;
    require(book.insert(limit_order(10, Side::Buy, 100, 2)), "first same-price order inserts");
    require(book.insert(limit_order(11, Side::Buy, 100, 3)), "second same-price order inserts");
    require(book.insert(limit_order(12, Side::Buy, 100, 5)), "third same-price order inserts");

    const std::vector<Order> orders = book.orders_at(Side::Buy, Price{100});
    require(orders.size() == 3, "same-price level contains three orders");
    require(orders[0].id() == OrderId{10}, "first order retains time priority");
    require(orders[1].id() == OrderId{11}, "second order retains time priority");
    require(orders[2].id() == OrderId{12}, "third order retains time priority");
    require(orders[0].sequence() < orders[1].sequence(), "sequence increases on insert");
    require(orders[1].sequence() < orders[2].sequence(), "sequence is deterministic");
    require(book.depth_at(Side::Buy, Price{100}) == std::optional<Qty>{Qty{10}},
            "same-price quantities aggregate");
}

void cancel_removes_order_and_empty_levels() {
    OrderBook book;
    require(book.insert(limit_order(20, Side::Buy, 100, 2)), "first order inserts");
    require(book.insert(limit_order(21, Side::Buy, 100, 3)), "second order inserts");
    require(book.insert(limit_order(22, Side::Sell, 105, 4)), "ask inserts");

    require(book.cancel(OrderId{20}), "existing order cancels");
    require(!book.contains(OrderId{20}), "cancelled order leaves index");
    require(book.depth_at(Side::Buy, Price{100}) == std::optional<Qty>{Qty{3}},
            "cancel reduces level depth");
    require(book.orders_at(Side::Buy, Price{100}).front().id() == OrderId{21},
            "remaining order stays at level");

    require(book.cancel(OrderId{21}), "last order at level cancels");
    require(book.level_count(Side::Buy) == 0, "empty price level is removed");
    require(!book.best_bid().has_value(), "best bid disappears after cancellation");
    require(!book.cancel(OrderId{20}), "repeated cancellation fails");
}

void reject_invalid_phase_one_orders() {
    OrderBook book;
    require(!book.insert(limit_order(30, Side::Buy, 100, 0)), "zero quantity is rejected");
    require(!book.insert(Order{OrderId{31}, Side::Buy, OrderType::Market, Price{100}, Qty{1}}),
            "market order is rejected before matching exists");
    require(!book.insert(limit_order(32, Side::Buy, 0, 1)), "non-positive price is rejected");
    require(book.insert(limit_order(33, Side::Buy, 100, 1)), "valid order inserts");
    require(!book.insert(limit_order(33, Side::Sell, 101, 1)), "duplicate ID is rejected");
}

void lookup_returns_order_snapshot() {
    OrderBook book;
    require(book.insert(limit_order(40, Side::Sell, 125, 9)), "order inserts");
    const auto found = book.find(OrderId{40});
    require(found.has_value(), "inserted order is findable");
    require(found->side() == Side::Sell, "lookup preserves side");
    require(found->price() == Price{125}, "lookup preserves price");
    require(found->quantity() == Qty{9}, "lookup preserves quantity");
}

} // namespace

int main() {
    empty_book_queries();
    insert_and_query_price_levels();
    preserve_fifo_order_at_a_level();
    cancel_removes_order_and_empty_levels();
    reject_invalid_phase_one_orders();
    lookup_returns_order_snapshot();
    std::cout << "All Meridian Phase 1 tests passed\n";
    return EXIT_SUCCESS;
}
