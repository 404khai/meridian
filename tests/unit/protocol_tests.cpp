#include "meridian/net/protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
using meridian::net::ProtocolSession;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void malformed_requests_do_not_mutate() {
    ProtocolSession session;
    const std::vector<std::string> invalid{
        "", " ", "UNKNOWN", "PING extra", "BOOK extra", "CANCEL 1", "LIMIT", "MARKET",
        "LIMIT 1 BUY 100", "LIMIT 1 BUY 100 2 extra", "MARKET 1 BUY 2 extra",
        "LIMIT -1 BUY 100 2", "LIMIT +1 BUY 100 2", "LIMIT 1 buy 100 2",
        "LIMIT 1 HOLD 100 2", "LIMIT 1 BUY -1 2", "LIMIT 1 BUY 0 2",
        "LIMIT 1 BUY 9223372036854775808 2", "LIMIT 18446744073709551616 BUY 100 2",
        "LIMIT 1 BUY 100 0", "LIMIT 1 BUY 100 -1", "LIMIT 1 BUY 100 1x",
        "LIMIT 1 BUY 100 18446744073709551616", "MARKET 1 BUY 1000000000001",
        "LIMIT 1 BUY 100 1000000000001", "PING\n", "PI\rNG", std::string{"PING\0", 5},
        std::string(257, 'x')};
    for (const auto& request : invalid) {
        require(session.execute(request) == "ERR BAD_REQUEST\n", "malformed request rejected");
        require(session.execute("BOOK") == "BOOK NONE NONE 0\n", "invalid input preserves book");
    }
    require(session.execute(" \tPING \t\r") == "PONG\n", "whitespace and CRLF supported");
    require(session.execute("LIMIT 0 BUY 9223372036854775807 1000000000000") ==
                "OK 0 1000000000000 0\nEND\n", "numeric upper boundaries accepted");
}

void matching_and_replay() {
    ProtocolSession first;
    ProtocolSession second;
    const std::vector<std::pair<std::string, std::string>> cases{
        {"BOOK", "BOOK NONE NONE 0\n"},
        {"LIMIT 1 SELL 101 2", "OK 1 2 0\nEND\n"},
        {"LIMIT 2 SELL 100 3", "OK 2 3 0\nEND\n"},
        {"LIMIT 3 SELL 100 4", "OK 3 4 0\nEND\n"},
        {"LIMIT 1 BUY 101 10", "REJECT 1\n"},
        {"LIMIT 4 BUY 100 5", "OK 4 0 2\nFILL 4 2 100 3\nFILL 4 3 100 2\nEND\n"},
        {"BOOK", "BOOK NONE 100 2\n"},
        {"MARKET 5 BUY 10", "OK 5 6 2\nFILL 5 3 100 2\nFILL 5 1 101 2\nEND\n"},
        {"BOOK", "BOOK NONE NONE 0\n"},
        {"LIMIT 6 BUY 99 2", "OK 6 2 0\nEND\n"},
        {"LIMIT 7 BUY 100 3", "OK 7 3 0\nEND\n"},
        {"LIMIT 8 SELL 99 7", "OK 8 2 2\nFILL 8 7 100 3\nFILL 8 6 99 2\nEND\n"},
        {"BOOK", "BOOK NONE 99 1\n"}};
    for (const auto& [request, expected] : cases) {
        require(first.execute(request) == expected && second.execute(request) == expected,
                "wire fills and replay preserve matching semantics");
    }
    require(!first.execute("LIMIT 9 BUY 90 1", 1), "response budget enforced");
    require(first.execute("BOOK") == "BOOK 90 99 2\n", "failed delivery cannot roll back order");
}

void bounded_book_admission() {
    ProtocolSession session;
    for (std::size_t i = 0; i < ProtocolSession::max_resting_orders; ++i) {
        const auto response = session.execute("LIMIT " + std::to_string(i) + " BUY 100 1");
        require(response && response->starts_with("OK "), "admit until configured order limit");
    }
    require(session.execute("LIMIT 100001 BUY 100 1") == "ERR BOOK_FULL\n",
            "resting book capacity bounds memory and aggregate quantities");
    const auto sweep = session.execute("MARKET 100002 SELL 100000");
    require(!sweep, "large fill response bounded after executing market sweep");
    require(session.execute("BOOK") == "BOOK NONE NONE 0\n", "capacity can be released by fills");
}
} // namespace

int main() {
    malformed_requests_do_not_mutate();
    matching_and_replay();
    bounded_book_admission();
    std::cout << "All protocol tests passed\n";
}
