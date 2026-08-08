#include "meridian/book/order_book.hpp"

int main() {
    // The Google Benchmark suite is intentionally deferred to Phase 6.
    const meridian::OrderBook book;
    return book.empty() ? 0 : 1;
}
