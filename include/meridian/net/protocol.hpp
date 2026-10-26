#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "meridian/engine/matching_engine.hpp"

namespace meridian::net {

// One owner, one shared instrument across all clients. No socket dependencies.
class ProtocolSession final {
public:
    static constexpr std::size_t max_line_bytes = 256;
    static constexpr std::size_t max_resting_orders = 100000;
    static constexpr std::uint64_t max_order_quantity = 1000000000000ULL;

    // A complete line without LF; optional trailing CR is accepted.
    // nullopt means the complete response exceeds the supplied buffer budget.
    // An executed request is never rolled back because its response cannot fit.
    [[nodiscard]] std::optional<std::string> execute(
        std::string_view line, std::size_t response_budget = 65536);

private:
    MatchingEngine engine_;
};

} // namespace meridian::net
