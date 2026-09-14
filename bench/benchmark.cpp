#include "order_book.hpp"
#include <random>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <vector>

// Generates a synthetic order flow (mostly adds, some cancels) around a
// fixed mid price and reports throughput + per-event latency percentiles.
// This is deliberately simple -- realistic order flow is bursty, has
// clustered cancels, and price walks over time, none of which this models.
// Good enough as a baseline to diff "before" against "after" once you start
// optimizing; not good enough to cite as a market-realistic result.
int main(int argc, char** argv) {
    size_t numOrders = 1'000'000;
    if (argc > 1) numOrders = std::stoul(argv[1]);

    // Pool capacity = numOrders: this single book absorbs all events (no
    // sharding here), so the number of orders ever resting at once can't
    // exceed the number submitted -- sizing the pool to numOrders is a
    // safe, tight upper bound.
    OrderBook book(20000, numOrders);
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int64_t> priceDist(9900, 10100); // ticks around 100.00
    std::uniform_int_distribution<uint32_t> qtyDist(1, 100);
    std::uniform_real_distribution<double> cancelDist(0.0, 1.0);

    std::vector<uint64_t> restingIds;
    restingIds.reserve(numOrders);
    std::vector<double> latenciesUs;
    latenciesUs.reserve(numOrders);

    const auto start = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 0; i < numOrders; ++i) {
        if (!restingIds.empty() && cancelDist(rng) < 0.10) {
            size_t idx = rng() % restingIds.size();
            const auto t0 = std::chrono::high_resolution_clock::now();
            book.cancelOrder(restingIds[idx]);
            const auto t1 = std::chrono::high_resolution_clock::now();
            latenciesUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());

            restingIds[idx] = restingIds.back();
            restingIds.pop_back();
            continue;
        }

        Order o{i,
                sideDist(rng) == 0 ? Side::Buy : Side::Sell,
                OrderType::Limit,
                priceDist(rng),
                qtyDist(rng),
                i};

        const auto t0 = std::chrono::high_resolution_clock::now();
        auto trades = book.addOrder(o);
        const auto t1 = std::chrono::high_resolution_clock::now();
        latenciesUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());

        if (trades.empty()) {
            restingIds.push_back(o.id);
        }
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const double totalMs = std::chrono::duration<double, std::milli>(end - start).count();

    std::sort(latenciesUs.begin(), latenciesUs.end());
    auto pct = [&](double p) {
        size_t idx = std::min(latenciesUs.size() - 1, static_cast<size_t>(p * latenciesUs.size()));
        return latenciesUs[idx];
    };

    std::cout << "events:        " << numOrders << "\n";
    std::cout << "wall time:     " << totalMs << " ms\n";
    std::cout << "throughput:    " << (numOrders / (totalMs / 1000.0)) << " events/sec\n";
    std::cout << "latency p50:   " << pct(0.50) << " us\n";
    std::cout << "latency p99:   " << pct(0.99) << " us\n";
    std::cout << "latency p999:  " << pct(0.999) << " us\n";
    std::cout << "latency max:   " << latenciesUs.back() << " us\n";

    return 0;
}
