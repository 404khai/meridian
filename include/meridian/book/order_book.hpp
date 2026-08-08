#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "meridian/order/order.hpp"

namespace meridian {

class OrderBook final {
public:
    OrderBook() = default;
    ~OrderBook() = default;

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = delete;
    OrderBook& operator=(OrderBook&&) = delete;

    // Phase 1 accepts resting limit orders only. Matching is intentionally absent.
    [[nodiscard]] bool insert(Order order);
    [[nodiscard]] bool cancel(OrderId id);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t order_count() const noexcept;
    [[nodiscard]] std::size_t level_count(Side side) const noexcept;
    [[nodiscard]] bool contains(OrderId id) const noexcept;
    [[nodiscard]] std::optional<Order> find(OrderId id) const;

    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;
    [[nodiscard]] std::optional<Qty> depth_at(Side side, Price price) const noexcept;
    [[nodiscard]] std::vector<Order> orders_at(Side side, Price price) const;

private:
    using OrderQueue = std::list<Order>;

    struct PriceLevel {
        OrderQueue orders;
        std::uint64_t quantity{0};
    };

    using Levels = std::map<Price, PriceLevel>;

    struct Location {
        Side side;
        Price price;
        OrderQueue::iterator iterator;
    };

    [[nodiscard]] Levels& levels_for(Side side) noexcept;
    [[nodiscard]] const Levels& levels_for(Side side) const noexcept;
    [[nodiscard]] PriceLevel* level_at(Side side, Price price) noexcept;
    [[nodiscard]] const PriceLevel* level_at(Side side, Price price) const noexcept;

    Levels bids_;
    Levels asks_;
    std::unordered_map<OrderId, Location> order_index_;
    std::uint64_t next_sequence_{0};
};

} // namespace meridian
