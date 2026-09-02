#include "meridian/concurrency/concurrent_matching_engine.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace meridian {

ConcurrentMatchingEngine::ConcurrentMatchingEngine()
    : snapshot_(std::make_shared<const BookSnapshot>()),
      worker_([this] { run(); }) {}

ConcurrentMatchingEngine::~ConcurrentMatchingEngine() {
    inbound_.close();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::future<SubmitResult> ConcurrentMatchingEngine::submit(Order order) {
    auto submission = std::make_shared<Submission>(std::move(order));
    auto result = submission->completion.get_future();
    if (!inbound_.push(submission)) {
        submission->completion.set_exception(
            std::make_exception_ptr(std::runtime_error{"concurrent matching engine is closed"}));
    }
    return result;
}

std::shared_ptr<const BookSnapshot> ConcurrentMatchingEngine::snapshot() const noexcept {
    return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
}

void ConcurrentMatchingEngine::run() {
    while (auto submission = inbound_.wait_pop()) {
        try {
            auto result = engine_.submit((*submission)->order);
            ++processed_submissions_;
            publish_snapshot();
            (*submission)->completion.set_value(std::move(result));
        } catch (...) {
            (*submission)->completion.set_exception(std::current_exception());
        }
    }
}

void ConcurrentMatchingEngine::publish_snapshot() {
    const auto& book = engine_.book();
    auto next = std::make_shared<const BookSnapshot>(BookSnapshot{
        book.best_bid(),
        book.best_ask(),
        book.order_count(),
        book.level_count(Side::Buy),
        book.level_count(Side::Sell),
        processed_submissions_,
    });
    std::atomic_store_explicit(&snapshot_, std::move(next), std::memory_order_release);
}

} // namespace meridian
