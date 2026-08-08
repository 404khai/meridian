#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace meridian {

class OrderId final {
public:
    constexpr explicit OrderId(std::uint64_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    constexpr auto operator<=>(const OrderId&) const = default;

private:
    std::uint64_t value_;
};

class Price final {
public:
    constexpr explicit Price(std::int64_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::int64_t value() const noexcept { return value_; }
    constexpr auto operator<=>(const Price&) const = default;

private:
    std::int64_t value_;
};

class Qty final {
public:
    constexpr explicit Qty(std::uint64_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    constexpr auto operator<=>(const Qty&) const = default;

private:
    std::uint64_t value_;
};

enum class Side : std::uint8_t {
    Buy,
    Sell,
};

enum class OrderType : std::uint8_t {
    Limit,
    Market,
};

} // namespace meridian

template <>
struct std::hash<meridian::OrderId> {
    std::size_t operator()(const meridian::OrderId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value());
    }
};
