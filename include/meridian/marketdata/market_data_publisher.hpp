#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "meridian/concurrency/snapshot_store.hpp"
#include "meridian/events/event.hpp"

namespace meridian {

struct DepthLevel final {
    Price price;
    Qty quantity;
    std::size_t order_count{0};
    bool operator==(const DepthLevel&) const = default;
};

struct MarketDataCounters final {
    std::uint64_t operations{0};
    std::uint64_t events{0};
    std::uint64_t accepted_orders{0};
    std::uint64_t rejected_orders{0};
    std::uint64_t trades{0};
    Qty traded_quantity{0};
    std::chrono::nanoseconds elapsed{0};
    std::chrono::nanoseconds total_latency{0};
    std::chrono::nanoseconds min_latency{0};
    std::chrono::nanoseconds max_latency{0};

    [[nodiscard]] double operations_per_second() const noexcept;
    [[nodiscard]] double mean_latency_ns() const noexcept;
};

struct MarketDataSnapshot final {
    std::uint64_t sequence{0};
    std::optional<Price> best_bid;
    std::optional<Price> best_ask;
    // Signed ticks; absent unless both sides exist. Direct books may be crossed.
    std::optional<std::int64_t> spread_ticks;
    std::vector<DepthLevel> bids; // Highest price first.
    std::vector<DepthLevel> asks; // Lowest price first.
    std::size_t order_count{0};
    MarketDataCounters counters;
};

// One event-consumer writer; any number of snapshot readers.
// Start with an empty source and publish every complete operation batch once,
// in source order. Latency is measured by the caller around the source operation.
class MarketDataPublisher final {
public:
    using Clock = std::chrono::steady_clock;

    explicit MarketDataPublisher(Clock::time_point started = Clock::now());
    MarketDataPublisher(const MarketDataPublisher&) = delete;
    MarketDataPublisher& operator=(const MarketDataPublisher&) = delete;
    MarketDataPublisher(MarketDataPublisher&&) = delete;
    MarketDataPublisher& operator=(MarketDataPublisher&&) = delete;

    void publish(std::span<const Event> events, std::chrono::nanoseconds latency,
                 Clock::time_point observed = Clock::now());
    [[nodiscard]] std::shared_ptr<const MarketDataSnapshot> snapshot() const noexcept;

private:
    struct RestingOrder final {
        Side side;
        Price price;
        Qty quantity;
    };
    using Orders = std::unordered_map<OrderId, RestingOrder>;

    static void apply(Orders& orders, const Event& event, MarketDataCounters& counters);
    static void reduce(Orders& orders, OrderId id, Qty quantity);
    static void build_depth(const Orders& orders, MarketDataSnapshot& next);

    Clock::time_point started_;
    Clock::time_point last_observed_;
    Orders orders_;
    SnapshotStore<MarketDataSnapshot> snapshot_;
};

} // namespace meridian
