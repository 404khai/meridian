#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace meridian {

template <typename T>
class BlockingQueue final {
public:
    BlockingQueue() = default;
    ~BlockingQueue() = default;

    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;
    BlockingQueue(BlockingQueue&&) = delete;
    BlockingQueue& operator=(BlockingQueue&&) = delete;

    [[nodiscard]] bool push(T value) {
        {
            const std::lock_guard lock{mutex_};
            if (closed_) {
                return false;
            }
            queue_.push(std::move(value));
        }
        ready_.notify_one();
        return true;
    }

    [[nodiscard]] std::optional<T> wait_pop() {
        std::unique_lock lock{mutex_};
        ready_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    void close() noexcept {
        {
            const std::lock_guard lock{mutex_};
            closed_ = true;
        }
        ready_.notify_all();
    }

    [[nodiscard]] bool closed() const noexcept {
        const std::lock_guard lock{mutex_};
        return closed_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const std::lock_guard lock{mutex_};
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::queue<T> queue_;
    bool closed_{false};
};

} // namespace meridian
