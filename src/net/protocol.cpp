#include "meridian/net/protocol.hpp"

#include <array>
#include <charconv>
#include <system_error>

namespace meridian::net {
namespace {

template <typename T>
bool number(std::string_view token, T& value) {
    if (token.empty()) {
        return false;
    }
    for (const char character : token) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

std::string price_text(std::optional<Price> price) {
    return price ? std::to_string(price->value()) : "NONE";
}

} // namespace

std::optional<std::string> ProtocolSession::execute(std::string_view line,
                                                   std::size_t response_budget) {
    const auto bounded = [response_budget](std::string response) -> std::optional<std::string> {
        if (response.size() > response_budget) {
            return std::nullopt;
        }
        return response;
    };
    if (line.size() > max_line_bytes) {
        return bounded("ERR BAD_REQUEST\n");
    }
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    std::array<std::string_view, 6> tokens{};
    std::size_t count = 0;
    while (!line.empty()) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos) {
            break;
        }
        line.remove_prefix(first);
        const auto length = line.find_first_of(" \t");
        const auto token = line.substr(0, length);
        if (count == tokens.size()) {
            return bounded("ERR BAD_REQUEST\n");
        }
        tokens[count++] = token;
        line.remove_prefix(token.size());
    }
    if (count == 1 && tokens[0] == "PING") {
        return bounded("PONG\n");
    }
    if (count == 1 && tokens[0] == "BOOK") {
        const auto& book = engine_.book();
        return bounded("BOOK " + price_text(book.best_bid()) + " " + price_text(book.best_ask()) +
                       " " + std::to_string(book.order_count()) + "\n");
    }
    const bool is_limit = count == 5 && tokens[0] == "LIMIT";
    const bool is_market = count == 4 && tokens[0] == "MARKET";
    std::uint64_t id = 0;
    std::int64_t price = 0;
    std::uint64_t quantity = 0;
    if ((!is_limit && !is_market) || !number(tokens[1], id) ||
        (tokens[2] != "BUY" && tokens[2] != "SELL") ||
        (is_limit && (!number(tokens[3], price) || price <= 0)) ||
        !number(tokens[is_limit ? 4 : 3], quantity) || quantity == 0 ||
        quantity > max_order_quantity) {
        return bounded("ERR BAD_REQUEST\n");
    }
    if (is_limit && engine_.book().order_count() >= max_resting_orders) {
        return bounded("ERR BOOK_FULL\n");
    }
    const auto result = engine_.submit(Order{
        OrderId{id}, tokens[2] == "BUY" ? Side::Buy : Side::Sell,
        is_limit ? OrderType::Limit : OrderType::Market, Price{price}, Qty{quantity}});
    // The TCP adapter owns this source log; no unbounded event backlog.
    (void)engine_.drain_events();
    if (!result.accepted) {
        return bounded("REJECT " + std::to_string(id) + "\n");
    }
    std::string response = "OK " + std::to_string(id) + " " +
        std::to_string(result.remaining.value()) + " " + std::to_string(result.fills.size()) + "\n";
    if (response.size() > response_budget) {
        return std::nullopt;
    }
    for (const auto& fill : result.fills) {
        const auto record = "FILL " + std::to_string(fill.aggressor_id.value()) + " " +
            std::to_string(fill.resting_id.value()) + " " + std::to_string(fill.price.value()) +
            " " + std::to_string(fill.quantity.value()) + "\n";
        if (record.size() > response_budget - response.size()) {
            return std::nullopt;
        }
        response += record;
    }
    if (response_budget - response.size() < 4) {
        return std::nullopt;
    }
    response += "END\n";
    return response;
}

} // namespace meridian::net
