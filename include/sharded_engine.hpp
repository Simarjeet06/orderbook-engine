#pragma once
#include "order.hpp"
#include "order_book.hpp"
#include "spsc_queue.hpp"
#include "affinity.hpp"
#include "timing.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Phase 2: symbol-sharded multithreading on top of the untouched, naive
// OrderBook from phase 1.
//
// This deliberately does NOT put a lock around a shared OrderBook. A single
// order book must process operations in one strict sequence, so locking it
// for concurrent access only adds contention and fattens the tail latency
// you're trying to shrink -- it does not add real parallelism, because the
// matching logic itself can't run concurrently on one book. What actually
// scales is running each *symbol's* book on its own thread, with zero shared
// mutable state between shards, and getting orders to the right shard via
// lock-free SPSC queues instead of a mutex.
//
// Producers (e.g. feed handler / gateway threads) each get one SPSC queue
// per shard they might route to. A shard drains all of its inbound queues
// and owns its OrderBook instances exclusively -- no atomics, no locks, no
// cache-line ping-pong inside the matching path itself.
// ---------------------------------------------------------------------------

enum class MsgType : uint8_t { Add = 0, Cancel = 1 };

struct Message {
    MsgType type;
    uint16_t symbol;
    Order order;         // valid when type == Add
    uint64_t cancelId;   // valid when type == Cancel
    uint64_t enqueueNs;
};

constexpr size_t kQueueCapacity = 1 << 16; // must stay a power of two
using MsgQueue = SpscQueue<Message, kQueueCapacity>;

class Shard {
public:
    Shard(int shardId, int numProducers, int affinityCore)
        : shardId_(shardId), affinityCore_(affinityCore) {
        queues_.reserve(numProducers);
        for (int i = 0; i < numProducers; ++i) {
            queues_.push_back(std::make_unique<MsgQueue>());
        }
        computeLatenciesNs_.reserve(1u << 22);
        queueLatenciesNs_.reserve(1u << 22);
    }

    MsgQueue& queueFor(int producerIdx) { return *queues_[producerIdx]; }

    void start() {
        running_.store(true, std::memory_order_relaxed);
        thread_ = std::thread([this] { run(); });
    }

    void stop() {
        running_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) thread_.join();
    }

    int id() const { return shardId_; }
    // Time inside book.addOrder()/cancelOrder() only -- directly comparable
    // to the single-threaded bench/benchmark.cpp numbers, since it excludes
    // any time a message spent waiting in its inbound queue.
    const std::vector<uint64_t>& computeLatenciesNs() const { return computeLatenciesNs_; }
    // enqueue -> processed, i.e. compute latency plus queue wait. Only
    // meaningful relative to the offered load: at or above a shard's
    // saturation throughput this is dominated by queueing delay, not by the
    // matching engine, so report it alongside the load it was measured at.
    const std::vector<uint64_t>& queueLatenciesNs() const { return queueLatenciesNs_; }
    uint64_t processed() const { return processed_; }
    uint64_t tradesGenerated() const { return tradesGenerated_; }

private:
    void run() {
        set_affinity_hint(affinityCore_);
        Message msg;
        while (running_.load(std::memory_order_relaxed)) {
            bool didWork = false;
            for (auto& q : queues_) {
                if (q->pop(msg)) {
                    applyAndRecord(msg);
                    didWork = true;
                }
            }
            if (!didWork) std::this_thread::yield();
        }
        // Drain whatever producers already enqueued before we exit -- a
        // real system would coordinate shutdown more carefully, but for a
        // benchmark run this just avoids discarding tail messages.
        for (auto& q : queues_) {
            while (q->pop(msg)) applyAndRecord(msg);
        }
    }

    void applyAndRecord(const Message& msg) {
        OrderBook& book = books_[msg.symbol];
        const uint64_t computeStart = now_ns();
        if (msg.type == MsgType::Add) {
            auto trades = book.addOrder(msg.order);
            tradesGenerated_ += trades.size();
        } else {
            book.cancelOrder(msg.cancelId);
        }
        const uint64_t computeEnd = now_ns();
        computeLatenciesNs_.push_back(computeEnd - computeStart);
        queueLatenciesNs_.push_back(computeEnd - msg.enqueueNs);
        ++processed_;
    }

    int shardId_;
    int affinityCore_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::vector<std::unique_ptr<MsgQueue>> queues_;
    std::unordered_map<uint16_t, OrderBook> books_;
    std::vector<uint64_t> computeLatenciesNs_;
    std::vector<uint64_t> queueLatenciesNs_;
    uint64_t processed_ = 0;
    uint64_t tradesGenerated_ = 0;
};

class ShardedEngine {
public:
    ShardedEngine(int numShards, int numProducers) : numShards_(numShards) {
        shards_.reserve(numShards);
        for (int s = 0; s < numShards; ++s) {
            shards_.push_back(std::make_unique<Shard>(s, numProducers, s));
        }
    }

    void start() { for (auto& s : shards_) s->start(); }
    void stop() { for (auto& s : shards_) s->stop(); }

    // Deterministic routing: same symbol always lands on the same shard, so
    // per-symbol price-time ordering is preserved even though shards run in
    // parallel across symbols.
    int shardFor(uint16_t symbol) const { return symbol % numShards_; }

    Shard& shard(int idx) { return *shards_[idx]; }
    int numShards() const { return numShards_; }

private:
    int numShards_;
    std::vector<std::unique_ptr<Shard>> shards_;
};
