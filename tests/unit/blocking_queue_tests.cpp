#include "meridian/concurrency/blocking_queue.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using meridian::BlockingQueue;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void multiple_producers_deliver_each_item_once() {
    constexpr std::uint64_t producer_count = 4;
    constexpr std::uint64_t items_per_producer = 5000;
    constexpr std::uint64_t item_count = producer_count * items_per_producer;

    BlockingQueue<std::uint64_t> queue;
    std::vector<std::uint64_t> consumed;
    consumed.reserve(item_count);
    std::atomic<bool> push_failed{false};

    std::thread consumer{[&] {
        while (auto value = queue.wait_pop()) {
            consumed.push_back(*value);
        }
    }};

    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::uint64_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            const std::uint64_t first = producer * items_per_producer;
            for (std::uint64_t offset = 0; offset < items_per_producer; ++offset) {
                if (!queue.push(first + offset)) {
                    push_failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }
    queue.close();
    consumer.join();

    require(!push_failed.load(std::memory_order_relaxed), "all producer pushes succeed");
    require(consumed.size() == item_count, "consumer receives every queued item");
    std::sort(consumed.begin(), consumed.end());
    for (std::uint64_t index = 0; index < item_count; ++index) {
        require(consumed[index] == index, "queue delivers each item exactly once");
    }
    require(queue.closed(), "queue reports closed state");
    require(queue.size() == 0, "queue is empty after draining");
    require(!queue.push(item_count), "closed queue rejects new items");
}

void close_unblocks_an_empty_consumer() {
    BlockingQueue<int> queue;
    std::atomic<bool> unblocked{false};
    std::atomic<bool> received_value{true};

    std::thread consumer{[&] {
        received_value.store(queue.wait_pop().has_value(), std::memory_order_relaxed);
        unblocked.store(true, std::memory_order_release);
    }};

    queue.close();
    consumer.join();
    require(unblocked.load(std::memory_order_acquire), "close unblocks waiting consumer");
    require(!received_value.load(std::memory_order_relaxed),
            "closed empty queue returns no value");
}

} // namespace

int main() {
    multiple_producers_deliver_each_item_once();
    close_unblocks_an_empty_consumer();
    std::cout << "All Meridian Phase 7 queue tests passed\n";
    return EXIT_SUCCESS;
}
