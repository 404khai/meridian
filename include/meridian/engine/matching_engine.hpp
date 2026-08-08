#pragma once

#include <cstdint>
#include <vector>

#include "meridian/book/order_book.hpp"

namespace meridian {

using Fill = Trade;

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
    [[nodiscard]] const EventLog& events() const noexcept;
    [[nodiscard]] std::vector<Event> drain_events();

private:
    [[nodiscard]] bool crosses(const Order& order, Price opposing_price) const noexcept;

    OrderBook book_;
};

} // namespace meridian
