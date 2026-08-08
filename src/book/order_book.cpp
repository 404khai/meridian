#include "meridian/book/order_book.hpp"

#include <utility>

namespace meridian {

bool OrderBook::insert(Order order) {
    const auto reason = validation_error(order, contains(order.id()));
    if (reason.has_value()) {
        append_event(OrderRejectedEvent{order.id(), *reason});
        return false;
    }

    return insert_impl(order, true);
}

bool OrderBook::insert_impl(Order order, bool emit_event) {

    order.sequence_ = next_sequence_++;
    auto& levels = levels_for(order.side());
    auto level_iterator = levels.try_emplace(order.price()).first;

    auto& level = level_iterator->second;
    level.orders.push_back(order);
    auto order_iterator = std::prev(level.orders.end());
    level.quantity += order.quantity().value();

    try {
        order_index_.emplace(order.id(), Location{order.side(), order.price(), order_iterator});
    } catch (...) {
        level.quantity -= order.quantity().value();
        level.orders.erase(order_iterator);
        if (level.orders.empty()) {
            levels.erase(level_iterator);
        }
        throw;
    }

    if (emit_event) {
        append_event(OrderAcceptedEvent{
            order.id(),
            order.side(),
            order.type(),
            order.price(),
            order.quantity(),
        });
    }

    return true;
}

bool OrderBook::cancel(OrderId id) {
    const auto index_iterator = order_index_.find(id);
    if (index_iterator == order_index_.end()) {
        return false;
    }

    const Location location = index_iterator->second;
    auto& levels = levels_for(location.side);
    auto level_iterator = levels.find(location.price);
    auto& level = level_iterator->second;
    level.quantity -= location.iterator->quantity().value();
    level.orders.erase(location.iterator);
    order_index_.erase(index_iterator);

    if (level.orders.empty()) {
        levels.erase(level_iterator);
    }

    append_event(OrderCancelledEvent{id});

    return true;
}

bool OrderBook::reduce_quantity(OrderId id, Qty quantity) {
    return reduce_quantity_impl(id, quantity, true);
}

bool OrderBook::reduce_quantity_impl(OrderId id, Qty quantity, bool emit_event) {
    if (quantity.value() == 0) {
        return false;
    }

    const auto index_iterator = order_index_.find(id);
    if (index_iterator == order_index_.end() ||
        quantity.value() > index_iterator->second.iterator->quantity().value()) {
        return false;
    }

    const Location location = index_iterator->second;
    const Qty old_quantity = location.iterator->quantity();
    auto& levels = levels_for(location.side);
    auto level_iterator = levels.find(location.price);
    auto& level = level_iterator->second;
    const Qty new_quantity{old_quantity.value() - quantity.value()};
    level.quantity -= quantity.value();

    if (new_quantity.value() == 0) {
        level.orders.erase(location.iterator);
        order_index_.erase(index_iterator);
        if (level.orders.empty()) {
            levels.erase(level_iterator);
        }
    } else {
        location.iterator->quantity_ = new_quantity;
    }

    if (emit_event) {
        append_event(OrderReducedEvent{id, old_quantity, new_quantity});
    }

    return true;
}

bool OrderBook::modify(OrderId id, Price price, Qty quantity) {
    if (price.value() <= 0 || quantity.value() == 0) {
        return false;
    }

    const auto index_iterator = order_index_.find(id);
    if (index_iterator == order_index_.end()) {
        return false;
    }

    const Location location = index_iterator->second;
    Order& order = *location.iterator;
    const Price old_price = location.price;
    const Qty old_quantity = order.quantity();

    if (old_price == price && old_quantity == quantity) {
        return true;
    }

    auto& old_levels = levels_for(location.side);
    auto old_level_iterator = old_levels.find(old_price);
    auto& old_level = old_level_iterator->second;

    const bool quantity_decreased = quantity.value() < old_quantity.value();
    if (old_price == price && quantity_decreased) {
        old_level.quantity -= old_quantity.value() - quantity.value();
        order.quantity_ = quantity;
        append_event(OrderModifiedEvent{
            id,
            old_price,
            old_quantity,
            price,
            quantity,
            true,
        });
        return true;
    }

    auto& new_levels = levels_for(location.side);
    auto new_level_iterator = new_levels.try_emplace(price).first;
    auto& new_level = new_level_iterator->second;

    // Splicing is allocation-free and keeps the operation atomic after the
    // destination price level has been created.
    new_level.orders.splice(new_level.orders.end(), old_level.orders, location.iterator);
    order.price_ = price;
    order.quantity_ = quantity;
    order.sequence_ = next_sequence_++;
    new_level.quantity += quantity.value();
    old_level.quantity -= old_quantity.value();
    index_iterator->second.price = price;
    index_iterator->second.iterator = std::prev(new_level.orders.end());

    if (old_level.orders.empty()) {
        old_levels.erase(old_level_iterator);
    }

    append_event(OrderModifiedEvent{
        id,
        old_price,
        old_quantity,
        price,
        quantity,
        false,
    });

    return true;
}

const EventLog& OrderBook::events() const noexcept {
    return event_log_;
}

std::vector<Event> OrderBook::drain_events() {
    return event_log_.drain();
}

bool OrderBook::empty() const noexcept {
    return order_index_.empty();
}

std::size_t OrderBook::order_count() const noexcept {
    return order_index_.size();
}

std::size_t OrderBook::level_count(Side side) const noexcept {
    return levels_for(side).size();
}

bool OrderBook::contains(OrderId id) const noexcept {
    return order_index_.contains(id);
}

std::optional<Order> OrderBook::find(OrderId id) const {
    const auto index_iterator = order_index_.find(id);
    if (index_iterator == order_index_.end()) {
        return std::nullopt;
    }

    return *index_iterator->second.iterator;
}

std::optional<Price> OrderBook::best_bid() const noexcept {
    if (bids_.empty()) {
        return std::nullopt;
    }

    return bids_.rbegin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) {
        return std::nullopt;
    }

    return asks_.begin()->first;
}

std::optional<Qty> OrderBook::depth_at(Side side, Price price) const noexcept {
    const auto* level = level_at(side, price);
    if (level == nullptr) {
        return std::nullopt;
    }

    return Qty{level->quantity};
}

std::vector<Order> OrderBook::orders_at(Side side, Price price) const {
    const auto* level = level_at(side, price);
    if (level == nullptr) {
        return {};
    }

    std::vector<Order> orders;
    orders.reserve(level->orders.size());
    for (const auto& order : level->orders) {
        orders.push_back(order);
    }
    return orders;
}

OrderBook::Levels& OrderBook::levels_for(Side side) noexcept {
    return side == Side::Buy ? bids_ : asks_;
}

const OrderBook::Levels& OrderBook::levels_for(Side side) const noexcept {
    return side == Side::Buy ? bids_ : asks_;
}

OrderBook::PriceLevel* OrderBook::level_at(Side side, Price price) noexcept {
    auto& levels = levels_for(side);
    const auto iterator = levels.find(price);
    return iterator == levels.end() ? nullptr : &iterator->second;
}

const OrderBook::PriceLevel* OrderBook::level_at(Side side, Price price) const noexcept {
    const auto& levels = levels_for(side);
    const auto iterator = levels.find(price);
    return iterator == levels.end() ? nullptr : &iterator->second;
}

std::optional<RejectReason> OrderBook::validation_error(
    const Order& order, bool duplicate_id) noexcept {
    const bool valid_side = order.side() == Side::Buy || order.side() == Side::Sell;
    if (!valid_side) {
        return RejectReason::InvalidSide;
    }
    if (order.type() != OrderType::Limit) {
        return RejectReason::UnsupportedOrderType;
    }
    if (order.quantity().value() == 0) {
        return RejectReason::InvalidQuantity;
    }
    if (order.price().value() <= 0) {
        return RejectReason::InvalidPrice;
    }
    if (duplicate_id) {
        return RejectReason::DuplicateOrderId;
    }
    return std::nullopt;
}

void OrderBook::append_event(Event event) {
    event_log_.append(std::move(event));
}

} // namespace meridian
