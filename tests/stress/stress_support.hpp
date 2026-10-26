#pragma once

#include "meridian/engine/matching_engine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace stress {
using namespace meridian;

inline void check(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error{std::string{message}}; }
}

class Generator final {
public:
    explicit Generator(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 7U;
        state_ ^= state_ << 17U;
        return state_;
    }
private:
    std::uint64_t state_;
};

// All observations, constant storage. 64 subdivisions per power of two.
// Quantiles are inclusive bucket upper bounds (less than 1/64 relative error
// above 64 ns); maxima are exact. A zero-clock sample maps to the 1 ns bucket.
struct Histogram final {
    std::array<std::uint64_t, 4096> buckets{};
    std::uint64_t count{0};
    std::uint64_t maximum{0};

    void observe(std::uint64_t ns) {
        maximum = std::max(maximum, ns);
        const auto value = std::max(std::uint64_t{1}, ns);
        const auto exponent = static_cast<unsigned>(std::bit_width(value) - 1U);
        const auto shift = exponent > 6U ? exponent - 6U : 0U;
        const auto sub = (value - (std::uint64_t{1} << exponent)) >> shift;
        ++buckets[exponent * 64U + sub];
        ++count;
    }
    void merge(const Histogram& other) {
        for (std::size_t i = 0; i < buckets.size(); ++i) { buckets[i] += other.buckets[i]; }
        count += other.count;
        maximum = std::max(maximum, other.maximum);
    }
    std::uint64_t percentile(unsigned numerator, unsigned denominator) const {
        check(numerator > 0 && numerator <= denominator, "invalid percentile");
        if (count == 0) { return 0; }
        const auto rank = (count * numerator + denominator - 1U) / denominator;
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < buckets.size(); ++i) {
            seen += buckets[i];
            if (seen >= rank) {
                const auto exponent = static_cast<unsigned>(i / 64U);
                const auto shift = exponent > 6U ? exponent - 6U : 0U;
                return (std::uint64_t{1} << exponent) +
                    ((static_cast<std::uint64_t>(i % 64U) + 1U) << shift) - 1U;
            }
        }
        throw std::runtime_error{"histogram count mismatch"};
    }
};

inline bool same_trade(const Trade& a, const Trade& b) {
    return a.aggressor_id == b.aggressor_id && a.resting_id == b.resting_id &&
        a.price == b.price && a.quantity == b.quantity;
}

struct Expected final {
    SubmitResult result;
    std::optional<RejectReason> rejection;
};

// Deliberately uses an unsorted vector and linear best-price selection instead
// of the production map/list/index structure. Vector order supplies FIFO ties.
class Reference final {
public:
    std::vector<Order> orders;

    Expected submit(const Order& input) {
        std::optional<RejectReason> reason;
        if (input.side() != Side::Buy && input.side() != Side::Sell) {
            reason = RejectReason::InvalidSide;
        } else if (input.type() != OrderType::Limit && input.type() != OrderType::Market) {
            reason = RejectReason::UnsupportedOrderType;
        } else if (input.quantity().value() == 0) {
            reason = RejectReason::InvalidQuantity;
        } else if (input.type() == OrderType::Limit && input.price().value() <= 0) {
            reason = RejectReason::InvalidPrice;
        } else if (std::any_of(orders.begin(), orders.end(), [&](const Order& resting) {
                       return resting.id() == input.id(); })) {
            reason = RejectReason::DuplicateOrderId;
        }
        if (reason) { return Expected{{}, reason}; }
        SubmitResult result;
        result.accepted = true;
        std::uint64_t remaining = input.quantity().value();
        while (remaining != 0) {
            auto best = orders.end();
            for (auto it = orders.begin(); it != orders.end(); ++it) {
                if (it->side() == input.side()) { continue; }
                if (input.type() == OrderType::Limit &&
                    (input.side() == Side::Buy ? it->price() > input.price()
                                              : it->price() < input.price())) { continue; }
                if (best == orders.end() || (input.side() == Side::Buy
                        ? it->price() < best->price() : it->price() > best->price())) {
                    best = it;
                }
            }
            if (best == orders.end()) { break; }
            const auto quantity = std::min(remaining, best->quantity().value());
            result.fills.push_back(Trade{input.id(), best->id(), best->price(), Qty{quantity}});
            remaining -= quantity;
            if (quantity == best->quantity().value()) {
                orders.erase(best);
            } else {
                *best = Order{best->id(), best->side(), best->type(), best->price(),
                              Qty{best->quantity().value() - quantity}};
            }
        }
        result.remaining = Qty{remaining};
        if (remaining != 0 && input.type() == OrderType::Limit) {
            orders.emplace_back(input.id(), input.side(), input.type(), input.price(), Qty{remaining});
        }
        return Expected{std::move(result), std::nullopt};
    }
};

inline void verify(const Order& input, const Expected& expected, const SubmitResult& actual,
                   const std::vector<Event>& events) {
    check(actual.accepted == expected.result.accepted &&
          actual.remaining == expected.result.remaining &&
          actual.fills.size() == expected.result.fills.size(), "result differs from reference");
    if (expected.rejection) {
        check(events.size() == 1, "rejection event missing/duplicated");
        const auto* rejected = std::get_if<OrderRejectedEvent>(&events[0]);
        check(rejected && rejected->id == input.id() && rejected->reason == *expected.rejection,
              "incorrect rejection event");
        return;
    }
    check(events.size() == 1 + 2 * expected.result.fills.size(), "event missing/duplicated");
    const auto* accepted = std::get_if<OrderAcceptedEvent>(&events[0]);
    check(accepted && accepted->id == input.id() && accepted->side == input.side() &&
          accepted->type == input.type() && accepted->price == input.price() &&
          accepted->quantity == input.quantity(), "incorrect acceptance event");
    for (std::size_t i = 0; i < actual.fills.size(); ++i) {
        const auto& fill = expected.result.fills[i];
        check(same_trade(actual.fills[i], fill), "fill differs from reference");
        const auto* matched = std::get_if<OrderMatchedEvent>(&events[1 + 2 * i]);
        const auto* trade = std::get_if<TradeGeneratedEvent>(&events[2 + 2 * i]);
        check(matched && matched->aggressor_id == fill.aggressor_id &&
              matched->resting_id == fill.resting_id && matched->price == fill.price &&
              matched->quantity == fill.quantity, "incorrect/reordered matched event");
        check(trade && same_trade(trade->trade, fill), "incorrect/reordered trade event");
    }
}
} // namespace stress
