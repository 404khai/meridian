#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "meridian/events/event.hpp"

namespace meridian {

class EventLog final {
public:
    EventLog() = default;

    void append(Event event) { events_.push_back(std::move(event)); }

    [[nodiscard]] bool empty() const noexcept { return events_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }
    [[nodiscard]] const Event& at(std::size_t index) const { return events_.at(index); }
    [[nodiscard]] const std::vector<Event>& entries() const noexcept { return events_; }

    void clear() noexcept { events_.clear(); }
    [[nodiscard]] std::vector<Event> drain() { return std::exchange(events_, {}); }

private:
    std::vector<Event> events_;
};

} // namespace meridian
