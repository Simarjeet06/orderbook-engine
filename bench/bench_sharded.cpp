#include "sharded_engine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

// Synthetic multi-symbol order flow spread across `numProducers` threads,
// routed by symbol to `numShards` independent OrderBook-owning threads.
// Same event mix as bench/benchmark.cpp (90% add / 10% cancel) so the two
// benchmarks' throughput and latency numbers are directly comparable --
// the only variable changing between runs is shard count.
//
// Usage: bench_sharded <numShards> <numProducers> <numSymbols> <totalOrders>
int main(int argc, char** argv) {
    int numShards = argc > 1 ? std::atoi(argv[1]) : 4;
    int numProducers = argc > 2 ? std::atoi(argv[2]) : 4;
    int numSymbols = argc > 3 ? std::atoi(argv[3]) : 64;
    uint64_t totalOrders = argc > 4 ? std::stoull(argv[4]) : 4'000'000ULL;

    ShardedEngine engine(numShards, numProducers);
    engine.start();

    std::atomic<uint64_t> globalId{0};
    std::vector<std::thread> producers;
    const uint64_t perProducer = totalOrders / static_cast<uint64_t>(numProducers);

    const auto start = std::chrono::steady_clock::now();

    for (int p = 0; p < numProducers; ++p) {
        producers.emplace_back([&, p] {
            std::mt19937_64 rng(1000 + p);
            std::uniform_int_distribution<int> sideDist(0, 1);
            std::uniform_int_distribution<int64_t> priceDist(9900, 10100);
            std::uniform_int_distribution<uint32_t> qtyDist(1, 100);
            std::uniform_int_distribution<int> symbolDist(0, numSymbols - 1);
            std::uniform_real_distribution<double> cancelDist(0.0, 1.0);

            // recent resting ids per symbol, for issuing plausible cancels
            std::vector<std::vector<uint64_t>> restingIds(numSymbols);

            for (uint64_t i = 0; i < perProducer; ++i) {
                const int symbol = symbolDist(rng);
                const int shardIdx = engine.shardFor(static_cast<uint16_t>(symbol));
                MsgQueue& q = engine.shard(shardIdx).queueFor(p);

                Message msg{};
                msg.symbol = static_cast<uint16_t>(symbol);

                auto& ids = restingIds[symbol];
                if (!ids.empty() && cancelDist(rng) < 0.10) {
                    size_t idx = rng() % ids.size();
                    msg.type = MsgType::Cancel;
                    msg.cancelId = ids[idx];
                    ids[idx] = ids.back();
                    ids.pop_back();
                } else {
                    const uint64_t id = globalId.fetch_add(1, std::memory_order_relaxed);
                    msg.type = MsgType::Add;
                    msg.order = Order{id,
                                       sideDist(rng) == 0 ? Side::Buy : Side::Sell,
                                       OrderType::Limit,
                                       priceDist(rng),
                                       qtyDist(rng),
                                       id};
                    ids.push_back(id);
                }
                msg.enqueueNs = now_ns();

                while (!q.push(msg)) std::this_thread::yield(); // backpressure
            }
        });
    }

    for (auto& t : producers) t.join();

    // Drain: wait until every shard has processed everything its queues
    // received before stopping the threads and reading out latencies.
    uint64_t produced = perProducer * static_cast<uint64_t>(numProducers);
    for (;;) {
        uint64_t processed = 0;
        for (int s = 0; s < engine.numShards(); ++s) processed += engine.shard(s).processed();
        if (processed >= produced) break;
        std::this_thread::yield();
    }

    const auto end = std::chrono::steady_clock::now();
    const double wallSeconds = std::chrono::duration<double>(end - start).count();

    engine.stop();

    std::vector<uint64_t> computeLat, queueLat;
    computeLat.reserve(produced);
    queueLat.reserve(produced);
    uint64_t totalTrades = 0;
    for (int s = 0; s < engine.numShards(); ++s) {
        const auto& c = engine.shard(s).computeLatenciesNs();
        const auto& q = engine.shard(s).queueLatenciesNs();
        computeLat.insert(computeLat.end(), c.begin(), c.end());
        queueLat.insert(queueLat.end(), q.begin(), q.end());
        totalTrades += engine.shard(s).tradesGenerated();
    }
    std::sort(computeLat.begin(), computeLat.end());
    std::sort(queueLat.begin(), queueLat.end());

    auto pct = [](const std::vector<uint64_t>& v, double p) -> double {
        if (v.empty()) return 0.0;
        size_t idx = std::min(v.size() - 1, static_cast<size_t>(p * v.size()));
        return v[idx] / 1000.0; // ns -> us
    };

    const double throughput = produced / wallSeconds;

    std::cout << "shards:            " << numShards << "\n";
    std::cout << "producers:         " << numProducers << "\n";
    std::cout << "symbols:           " << numSymbols << "\n";
    std::cout << "events:            " << produced << "\n";
    std::cout << "trades:            " << totalTrades << "\n";
    std::cout << "wall time:         " << (wallSeconds * 1000.0) << " ms\n";
    std::cout << "throughput:        " << throughput << " events/sec\n";
    std::cout << "-- compute latency (matching only, comparable to bench/benchmark.cpp) --\n";
    std::cout << "compute p50:       " << pct(computeLat, 0.50) << " us\n";
    std::cout << "compute p99:       " << pct(computeLat, 0.99) << " us\n";
    std::cout << "compute p999:      " << pct(computeLat, 0.999) << " us\n";
    std::cout << "compute max:       " << (computeLat.empty() ? 0.0 : computeLat.back() / 1000.0) << " us\n";
    std::cout << "-- end-to-end latency (compute + queue wait, at this run's offered load) --\n";
    std::cout << "e2e p50:           " << pct(queueLat, 0.50) << " us\n";
    std::cout << "e2e p99:           " << pct(queueLat, 0.99) << " us\n";
    std::cout << "e2e p999:          " << pct(queueLat, 0.999) << " us\n";
    std::cout << "e2e max:           " << (queueLat.empty() ? 0.0 : queueLat.back() / 1000.0) << " us\n";

    // Machine-readable line for the sweep script to append to a CSV.
    std::cout << "CSV," << numShards << "," << numProducers << "," << numSymbols << ","
              << produced << "," << wallSeconds << "," << throughput << ","
              << pct(computeLat, 0.50) << "," << pct(computeLat, 0.99) << "," << pct(computeLat, 0.999) << ","
              << pct(queueLat, 0.50) << "," << pct(queueLat, 0.99) << "," << pct(queueLat, 0.999) << "\n";

    return 0;
}
