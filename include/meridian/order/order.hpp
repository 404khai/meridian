#pragma once

#include <cstdint>

#include "meridian/core/types.hpp"

namespace meridian {

class Order final {
public:
    constexpr Order(OrderId id, Side side, OrderType type, Price price, Qty quantity) noexcept
        : id_(id), side_(side), type_(type), price_(price), quantity_(quantity) {}

    [[nodiscard]] constexpr OrderId id() const noexcept { return id_; }
    [[nodiscard]] constexpr Side side() const noexcept { return side_; }
    [[nodiscard]] constexpr OrderType type() const noexcept { return type_; }
    [[nodiscard]] constexpr Price price() const noexcept { return price_; }
    [[nodiscard]] constexpr Qty quantity() const noexcept { return quantity_; }
    [[nodiscard]] constexpr std::uint64_t sequence() const noexcept { return sequence_; }

private:
    friend class OrderBook;

    OrderId id_;
    Side side_;
    OrderType type_;
    Price price_;
    Qty quantity_;
    std::uint64_t sequence_{0};
};

} // namespace meridian
