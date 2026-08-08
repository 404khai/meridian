#include "meridian/book/order_book.hpp"

namespace meridian {

bool OrderBook::insert(Order order) {
    const bool valid_side = order.side() == Side::Buy || order.side() == Side::Sell;
    if (!valid_side || order.type() != OrderType::Limit || order.quantity().value() == 0 ||
        order.price().value() <= 0 || contains(order.id())) {
        return false;
    }

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

    return true;
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

} // namespace meridian
