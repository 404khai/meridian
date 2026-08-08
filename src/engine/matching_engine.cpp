#include "meridian/engine/matching_engine.hpp"

#include <algorithm>
#include <optional>

namespace meridian {

SubmitResult MatchingEngine::submit(const Order& order) {
    SubmitResult result;
    const bool valid_side = order.side() == Side::Buy || order.side() == Side::Sell;
    const bool valid_type = order.type() == OrderType::Limit || order.type() == OrderType::Market;
    const bool duplicate_id = book_.contains(order.id());
    if (!valid_side || !valid_type || duplicate_id || order.quantity().value() == 0 ||
        (order.type() == OrderType::Limit && order.price().value() <= 0)) {
        const auto reason = !valid_side
                                ? RejectReason::InvalidSide
                                : !valid_type
                                      ? RejectReason::UnsupportedOrderType
                                      : order.quantity().value() == 0
                                            ? RejectReason::InvalidQuantity
                                            : order.type() == OrderType::Limit &&
                                                      order.price().value() <= 0
                                                  ? RejectReason::InvalidPrice
                                                  : RejectReason::DuplicateOrderId;
        book_.append_event(OrderRejectedEvent{order.id(), reason});
        return result;
    }

    book_.append_event(OrderAcceptedEvent{
        order.id(),
        order.side(),
        order.type(),
        order.price(),
        order.quantity(),
    });

    std::uint64_t remaining = order.quantity().value();
    while (remaining > 0) {
        const std::optional<Price> opposing_price =
            order.side() == Side::Buy ? book_.best_ask() : book_.best_bid();
        if (!opposing_price.has_value() || !crosses(order, *opposing_price)) {
            break;
        }

        const std::vector<Order> resting_orders =
            book_.orders_at(order.side() == Side::Buy ? Side::Sell : Side::Buy, *opposing_price);
        if (resting_orders.empty()) {
            break;
        }

        const Order& resting_order = resting_orders.front();
        const std::uint64_t fill_quantity =
            std::min(remaining, resting_order.quantity().value());
        result.fills.push_back(Fill{
            order.id(),
            resting_order.id(),
            resting_order.price(),
            Qty{fill_quantity},
        });
        const bool reduced = book_.reduce_quantity_impl(resting_order.id(), Qty{fill_quantity}, false);
        if (!reduced) {
            result.fills.pop_back();
            break;
        }
        book_.append_event(OrderMatchedEvent{
            order.id(),
            resting_order.id(),
            resting_order.price(),
            Qty{fill_quantity},
        });
        book_.append_event(TradeGeneratedEvent{Trade{
            order.id(),
            resting_order.id(),
            resting_order.price(),
            Qty{fill_quantity},
        }});
        remaining -= fill_quantity;
    }

    if (remaining > 0 && order.type() == OrderType::Limit) {
        const Order remainder{
            order.id(),
            order.side(),
            OrderType::Limit,
            order.price(),
            Qty{remaining},
        };
        if (!book_.insert_impl(remainder, false)) {
            result.fills.clear();
            return result;
        }
    }

    result.accepted = true;
    result.remaining = Qty{remaining};
    return result;
}

const OrderBook& MatchingEngine::book() const noexcept {
    return book_;
}

const EventLog& MatchingEngine::events() const noexcept {
    return book_.events();
}

std::vector<Event> MatchingEngine::drain_events() {
    return book_.drain_events();
}

bool MatchingEngine::crosses(const Order& order, Price opposing_price) const noexcept {
    if (order.type() == OrderType::Market) {
        return true;
    }

    return order.side() == Side::Buy ? opposing_price <= order.price()
                                     : opposing_price >= order.price();
}

} // namespace meridian
