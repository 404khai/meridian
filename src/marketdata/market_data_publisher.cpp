#include "meridian/marketdata/market_data_publisher.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace meridian {
namespace {

std::uint64_t add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error{"market-data counter overflow"};
    }
    return left + right;
}

void validate(Side side, Price price, Qty quantity) {
    if ((side != Side::Buy && side != Side::Sell) || price.value() <= 0 ||
        quantity.value() == 0) {
        throw std::invalid_argument{"invalid resting-order event"};
    }
}

} // namespace

double MarketDataCounters::operations_per_second() const noexcept {
    return elapsed.count() > 0
               ? static_cast<double>(operations) * 1e9 / static_cast<double>(elapsed.count())
               : 0.0;
}

double MarketDataCounters::mean_latency_ns() const noexcept {
    return operations > 0
               ? static_cast<double>(total_latency.count()) / static_cast<double>(operations)
               : 0.0;
}

MarketDataPublisher::MarketDataPublisher(Clock::time_point started)
    : started_(started), last_observed_(started),
      snapshot_(std::make_shared<const MarketDataSnapshot>()) {}

std::shared_ptr<const MarketDataSnapshot> MarketDataPublisher::snapshot() const noexcept {
    return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
}

void MarketDataPublisher::reduce(Orders& orders, OrderId id, Qty quantity) {
    const auto it = orders.find(id);
    if (it == orders.end() || quantity.value() == 0 ||
        quantity.value() > it->second.quantity.value()) {
        throw std::invalid_argument{"inconsistent quantity reduction event"};
    }
    const auto remaining = it->second.quantity.value() - quantity.value();
    if (remaining == 0) {
        orders.erase(it);
    } else {
        it->second.quantity = Qty{remaining};
    }
}

void MarketDataPublisher::apply(Orders& orders, const Event& event,
                                MarketDataCounters& counters) {
    counters.events = add(counters.events, 1);
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, OrderAcceptedEvent>) {
            counters.accepted_orders = add(counters.accepted_orders, 1);
            if (value.type == OrderType::Limit) {
                validate(value.side, value.price, value.quantity);
                if (!orders.emplace(value.id, RestingOrder{
                        value.side, value.price, value.quantity}).second) {
                    throw std::invalid_argument{"duplicate accepted order event"};
                }
            } else if (value.type != OrderType::Market) {
                throw std::invalid_argument{"invalid accepted order type"};
            }
        } else if constexpr (std::is_same_v<T, OrderRejectedEvent>) {
            counters.rejected_orders = add(counters.rejected_orders, 1);
        } else if constexpr (std::is_same_v<T, OrderCancelledEvent>) {
            if (orders.erase(value.id) != 1) {
                throw std::invalid_argument{"unknown cancelled order"};
            }
        } else if constexpr (std::is_same_v<T, OrderModifiedEvent>) {
            const auto it = orders.find(value.id);
            if (it == orders.end() || it->second.price != value.old_price ||
                it->second.quantity != value.old_quantity) {
                throw std::invalid_argument{"inconsistent modification event"};
            }
            validate(it->second.side, value.new_price, value.new_quantity);
            it->second.price = value.new_price;
            it->second.quantity = value.new_quantity;
        } else if constexpr (std::is_same_v<T, OrderReducedEvent>) {
            const auto it = orders.find(value.id);
            if (it == orders.end() || it->second.quantity != value.old_quantity ||
                value.new_quantity.value() >= value.old_quantity.value()) {
                throw std::invalid_argument{"inconsistent reduction event"};
            }
            reduce(orders, value.id, Qty{value.old_quantity.value() - value.new_quantity.value()});
        } else if constexpr (std::is_same_v<T, OrderMatchedEvent>) {
            // Market aggressors never enter the resting projection. Limit
            // aggressors enter on acceptance and lose their executed quantity.
            reduce(orders, value.resting_id, value.quantity);
            if (orders.contains(value.aggressor_id)) {
                reduce(orders, value.aggressor_id, value.quantity);
            }
        } else if constexpr (std::is_same_v<T, TradeGeneratedEvent>) {
            // Matches already changed depth; trades only update observability.
            counters.trades = add(counters.trades, 1);
            counters.traded_quantity = Qty{add(counters.traded_quantity.value(),
                                               value.trade.quantity.value())};
        }
    }, event);
}

void MarketDataPublisher::build_depth(const Orders& orders, MarketDataSnapshot& next) {
    std::map<Price, DepthLevel> bids;
    std::map<Price, DepthLevel> asks;
    for (const auto& [id, order] : orders) {
        (void)id;
        auto& levels = order.side == Side::Buy ? bids : asks;
        auto& level = levels.try_emplace(order.price, DepthLevel{order.price, Qty{0}, 0})
                          .first->second;
        level.quantity = Qty{add(level.quantity.value(), order.quantity.value())};
        ++level.order_count;
    }
    for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
        next.bids.push_back(it->second);
    }
    for (const auto& [price, level] : asks) {
        (void)price;
        next.asks.push_back(level);
    }
    next.order_count = orders.size();
    if (!next.bids.empty()) {
        next.best_bid = next.bids.front().price;
    }
    if (!next.asks.empty()) {
        next.best_ask = next.asks.front().price;
    }
    if (next.best_bid && next.best_ask) {
        next.spread_ticks = next.best_ask->value() - next.best_bid->value();
    }
}

void MarketDataPublisher::publish(std::span<const Event> events,
                                   std::chrono::nanoseconds latency,
                                   Clock::time_point observed) {
    if (latency.count() < 0 || observed < last_observed_) {
        throw std::invalid_argument{"negative latency or backwards observation time"};
    }
    // Stage the projection and allocation before committing. Failed batches
    // preserve both the previous projection and the published snapshot.
    auto orders = orders_;
    auto next = std::make_shared<MarketDataSnapshot>();
    next->counters = snapshot()->counters;
    auto& counters = next->counters;
    counters.operations = add(counters.operations, 1);
    next->sequence = counters.operations;
    counters.elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(observed - started_);
    const auto total = add(static_cast<std::uint64_t>(counters.total_latency.count()),
                           static_cast<std::uint64_t>(latency.count()));
    if (total > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error{"market-data latency overflow"};
    }
    counters.total_latency = std::chrono::nanoseconds{static_cast<std::int64_t>(total)};
    counters.min_latency = counters.operations == 1 ? latency : std::min(counters.min_latency, latency);
    counters.max_latency = std::max(counters.max_latency, latency);
    for (const auto& event : events) {
        apply(orders, event, counters);
    }
    build_depth(orders, *next);
    orders_.swap(orders);
    last_observed_ = observed;
    std::shared_ptr<const MarketDataSnapshot> published = std::move(next);
    std::atomic_store_explicit(&snapshot_, std::move(published), std::memory_order_release);
}

} // namespace meridian
