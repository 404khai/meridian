#include "meridian/engine/matching_engine.hpp"

#include <iostream>

int main() {
    using namespace meridian;
    MatchingEngine engine;
    if (!engine.submit(Order{OrderId{1}, Side::Sell, OrderType::Limit, Price{101}, Qty{4}}).accepted ||
        !engine.submit(Order{OrderId{2}, Side::Sell, OrderType::Limit, Price{100}, Qty{3}}).accepted ||
        !engine.submit(Order{OrderId{3}, Side::Sell, OrderType::Limit, Price{100}, Qty{2}}).accepted) {
        return 1;
    }
    (void)engine.drain_events();
    const auto result = engine.submit(
        Order{OrderId{4}, Side::Buy, OrderType::Limit, Price{101}, Qty{7}});
    const auto events = engine.drain_events();
    const auto ask = engine.book().best_ask();
    const auto depth = engine.book().depth_at(Side::Sell, Price{101});
    if (!result.accepted || result.remaining != Qty{0} || result.fills.size() != 3 ||
        events.size() != 7 || ask != Price{101} || depth != Qty{2}) {
        return 1;
    }
    // Presentation occurs after submission, matching, and event collection.
    for (const auto& fill : result.fills) {
        std::cout << "maker=" << fill.resting_id.value() << " price=" << fill.price.value()
                  << " quantity=" << fill.quantity.value() << '\n';
    }
    std::cout << "best_ask=" << ask->value() << " depth=" << depth->value()
              << " events=" << events.size() << '\n';
}
