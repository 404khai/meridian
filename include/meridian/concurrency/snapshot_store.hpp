#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace meridian {

// Immutable snapshot ownership with acquire/release publication. Older libc++
// lacks atomic<shared_ptr>; newer libstdc++ deprecates the free-function API.
template <typename T>
class SnapshotStore final {
public:
    explicit SnapshotStore(std::shared_ptr<const T> initial) : value_(std::move(initial)) {}
    SnapshotStore(const SnapshotStore&) = delete;
    SnapshotStore& operator=(const SnapshotStore&) = delete;

    [[nodiscard]] std::shared_ptr<const T> load() const noexcept {
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
        return value_.load(std::memory_order_acquire);
#else
        return std::atomic_load_explicit(&value_, std::memory_order_acquire);
#endif
    }

    void store(std::shared_ptr<const T> next) noexcept {
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
        value_.store(std::move(next), std::memory_order_release);
#else
        std::atomic_store_explicit(&value_, std::move(next), std::memory_order_release);
#endif
    }

private:
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    std::atomic<std::shared_ptr<const T>> value_;
#else
    std::shared_ptr<const T> value_;
#endif
};

} // namespace meridian
