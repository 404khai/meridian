#include "meridian/book/order_book.hpp"

int main() {
    const meridian::OrderBook book;
    return book.empty() ? 0 : 1;
}
