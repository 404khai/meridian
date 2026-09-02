#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include "meridian/concurrency/blocking_queue.hpp"
#include "meridian/engine/matching_engine.hpp"

namespace meridian {

struct BookSnapshot final {
    std::optional<Price> best_bid;
    std::optional<Price> best_ask;
    std::size_t order_count{0};
    std::size_t bid_level_count{0};
    std::size_t ask_level_count{0};
    std::uint64_t processed_submissions{0};
};

class ConcurrentMatchingEngine final {
public:
    ConcurrentMatchingEngine();
    ~ConcurrentMatchingEngine();

    ConcurrentMatchingEngine(const ConcurrentMatchingEngine&) = delete;
    ConcurrentMatchingEngine& operator=(const ConcurrentMatchingEngine&) = delete;
    ConcurrentMatchingEngine(ConcurrentMatchingEngine&&) = delete;
    ConcurrentMatchingEngine& operator=(ConcurrentMatchingEngine&&) = delete;

    [[nodiscard]] std::future<SubmitResult> submit(Order order);
    [[nodiscard]] std::shared_ptr<const BookSnapshot> snapshot() const noexcept;

private:
    struct Submission final {
        explicit Submission(Order submitted_order) : order(std::move(submitted_order)) {}

        Order order;
        std::promise<SubmitResult> completion;
    };

    void run();
    void publish_snapshot();

    MatchingEngine engine_;
    BlockingQueue<std::shared_ptr<Submission>> inbound_;
    std::shared_ptr<const BookSnapshot> snapshot_;
    std::thread worker_;
    std::uint64_t processed_submissions_{0};
};

} // namespace meridian
