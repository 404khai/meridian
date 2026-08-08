#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "meridian/book/order_book.hpp"

namespace meridian {

struct Fill final {
    OrderId aggressor_id;
    OrderId resting_id;
    Price price;
    Qty quantity;
};

struct SubmitResult final {
    bool accepted{false};
    Qty remaining{0};
    std::vector<Fill> fills;
};

class MatchingEngine final {
public:
    MatchingEngine() = default;

    [[nodiscard]] SubmitResult submit(const Order& order);
    [[nodiscard]] const OrderBook& book() const noexcept;

private:
    [[nodiscard]] bool crosses(const Order& order, Price opposing_price) const noexcept;

    OrderBook book_;
};

} // namespace meridian
