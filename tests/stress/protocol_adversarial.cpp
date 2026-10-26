#include "stress_support.hpp"
#include "meridian/net/protocol.hpp"

#include <iostream>
#include <string>

int main() {
    try {
        stress::Generator generator{8675309};
        meridian::net::ProtocolSession session;
        stress::check(session.execute("LIMIT 1 BUY 100 1000000") == "OK 1 1000000 0\nEND\n",
                      "adversarial protocol fixture failed");
        std::uint64_t fills = 0;
        for (std::uint64_t i = 0; i < 20000; ++i) {
            // Every generated request is provably invalid: unknown command
            // prefix, no LF, arbitrary binary/control bytes and overlong lines.
            std::string invalid{"!"};
            const auto length = generator.next() % 300;
            for (std::uint64_t byte = 0; byte < length; ++byte) {
                const auto value = generator.next() % 256;
                invalid.push_back(static_cast<char>(value == 10 ? 0 : value));
            }
            stress::check(session.execute(invalid) == "ERR BAD_REQUEST\n", "invalid frame accepted");
            if (i % 32 == 0) {
                const auto id = i + 2;
                const auto expected = "OK " + std::to_string(id) + " 0 1\nFILL " +
                    std::to_string(id) + " 1 100 1\nEND\n";
                stress::check(session.execute("MARKET " + std::to_string(id) + " SELL 1") == expected,
                              "malformed traffic corrupted later matching");
                ++fills;
                stress::check(session.execute("PING") == "PONG\n" &&
                              session.execute("BOOK") == "BOOK 100 NONE 1\n", "stream recovery failed");
            }
        }
        const auto expected = "OK 30000 " + std::to_string(fills) + " 1\nFILL 30000 1 100 " +
            std::to_string(1000000 - fills) + "\nEND\n";
        stress::check(session.execute("MARKET 30000 SELL 1000000") == expected &&
                      session.execute("BOOK") == "BOOK NONE NONE 0\n", "adversarial quantity drift");
        std::cout << "20000 seeded malformed frames; valid interleaved fills conserved quantity\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
