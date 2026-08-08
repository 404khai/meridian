#include "meridian/book/order_book.hpp"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string_view>

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

void quantity_decrease_preserves_priority() {
    OrderBook book;
    require(book.insert(limit_order(1, Side::Buy, 100, 10)), "first order inserts");
    require(book.insert(limit_order(2, Side::Buy, 100, 5)), "second order inserts");
    const auto before = book.find(OrderId{1});

    require(book.modify(OrderId{1}, Price{100}, Qty{4}), "quantity decrease succeeds");
    const auto after = book.find(OrderId{1});
    require(after->quantity() == Qty{4}, "quantity decreases");
    require(after->sequence() == before->sequence(), "quantity decrease preserves sequence");
    require(book.orders_at(Side::Buy, Price{100}).front().id() == OrderId{1},
            "quantity decrease preserves FIFO position");
    require(book.depth_at(Side::Buy, Price{100}) == std::optional<Qty>{Qty{9}},
            "quantity decrease updates depth");
}

void quantity_increase_loses_priority() {
    OrderBook book;
    require(book.insert(limit_order(10, Side::Buy, 100, 2)), "first order inserts");
    require(book.insert(limit_order(11, Side::Buy, 100, 3)), "second order inserts");
    const auto before = book.find(OrderId{10});

    require(book.modify(OrderId{10}, Price{100}, Qty{5}), "quantity increase succeeds");
    const auto orders = book.orders_at(Side::Buy, Price{100});
    require(orders.front().id() == OrderId{11}, "quantity increase loses FIFO position");
    require(orders.back().id() == OrderId{10}, "increased order moves to queue back");
    require(orders.back().sequence() > before->sequence(), "quantity increase gets new sequence");
    require(book.depth_at(Side::Buy, Price{100}) == std::optional<Qty>{Qty{8}},
            "quantity increase updates depth");
}

void price_change_loses_priority_and_moves_levels() {
    OrderBook book;
    require(book.insert(limit_order(20, Side::Sell, 105, 2)), "first ask inserts");
    require(book.insert(limit_order(21, Side::Sell, 110, 3)), "second ask inserts");
    const auto before = book.find(OrderId{20});

    require(book.modify(OrderId{20}, Price{110}, Qty{1}), "price change succeeds");
    require(!book.depth_at(Side::Sell, Price{105}).has_value(), "old level is removed");
    require(book.depth_at(Side::Sell, Price{110}) == std::optional<Qty>{Qty{4}},
            "new level contains updated depth");
    const auto orders = book.orders_at(Side::Sell, Price{110});
    require(orders.front().id() == OrderId{21}, "price change loses priority at destination");
    require(orders.back().id() == OrderId{20}, "price-changed order goes to destination back");
    require(orders.back().sequence() > before->sequence(), "price change gets new sequence");
}

void unchanged_modification_preserves_order_exactly() {
    OrderBook book;
    require(book.insert(limit_order(30, Side::Buy, 100, 7)), "order inserts");
    const auto before = book.find(OrderId{30});

    require(book.modify(OrderId{30}, Price{100}, Qty{7}), "unchanged modify succeeds");
    const auto after = book.find(OrderId{30});
    require(after->sequence() == before->sequence(), "unchanged modify preserves sequence");
    require(book.order_count() == 1, "unchanged modify preserves order count");
}

void invalid_and_missing_modifications_do_not_mutate() {
    OrderBook book;
    require(book.insert(limit_order(40, Side::Buy, 100, 7)), "order inserts");
    const auto before = book.find(OrderId{40});

    require(!book.modify(OrderId{999}, Price{101}, Qty{2}), "missing order is rejected");
    require(!book.modify(OrderId{40}, Price{0}, Qty{2}), "non-positive price is rejected");
    require(!book.modify(OrderId{40}, Price{101}, Qty{0}), "zero quantity is rejected");
    const auto after = book.find(OrderId{40});
    require(after->price() == before->price(), "invalid modify preserves price");
    require(after->quantity() == before->quantity(), "invalid modify preserves quantity");
    require(after->sequence() == before->sequence(), "invalid modify preserves sequence");
}

void modify_after_fill_and_cancel_is_rejected() {
    OrderBook book;
    require(book.insert(limit_order(50, Side::Buy, 100, 5)), "order inserts");
    require(book.reduce_quantity(OrderId{50}, Qty{3}), "partial fill succeeds");
    require(book.modify(OrderId{50}, Price{101}, Qty{4}), "remaining order can be modified");
    require(book.cancel(OrderId{50}), "modified order cancels");
    require(!book.modify(OrderId{50}, Price{102}, Qty{1}),
            "modify after cancellation is rejected");
    require(!book.contains(OrderId{50}), "cancelled order remains absent");
}

} // namespace

int main() {
    quantity_decrease_preserves_priority();
    quantity_increase_loses_priority();
    price_change_loses_priority_and_moves_levels();
    unchanged_modification_preserves_order_exactly();
    invalid_and_missing_modifications_do_not_mutate();
    modify_after_fill_and_cancel_is_rejected();
    std::cout << "All Meridian Phase 3 tests passed\n";
    return EXIT_SUCCESS;
}
