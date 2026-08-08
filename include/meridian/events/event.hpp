#pragma once

#include <cstdint>
#include <variant>

#include "meridian/order/order.hpp"

namespace meridian {

enum class RejectReason : std::uint8_t {
    InvalidSide,
    UnsupportedOrderType,
    InvalidQuantity,
    InvalidPrice,
    DuplicateOrderId,
};

struct OrderAcceptedEvent final {
    OrderId id;
    Side side;
    OrderType type;
    Price price;
    Qty quantity;
};

struct OrderRejectedEvent final {
    OrderId id;
    RejectReason reason;
};

struct OrderCancelledEvent final {
    OrderId id;
};

struct OrderModifiedEvent final {
    OrderId id;
    Price old_price;
    Qty old_quantity;
    Price new_price;
    Qty new_quantity;
    bool priority_preserved;
};

struct OrderReducedEvent final {
    OrderId id;
    Qty old_quantity;
    Qty new_quantity;
};

struct Trade final {
    OrderId aggressor_id;
    OrderId resting_id;
    Price price;
    Qty quantity;
};

struct OrderMatchedEvent final {
    OrderId aggressor_id;
    OrderId resting_id;
    Price price;
    Qty quantity;
};

struct TradeGeneratedEvent final {
    Trade trade;
};

using Event = std::variant<
    OrderAcceptedEvent,
    OrderRejectedEvent,
    OrderCancelledEvent,
    OrderModifiedEvent,
    OrderReducedEvent,
    OrderMatchedEvent,
    TradeGeneratedEvent>;

} // namespace meridian
