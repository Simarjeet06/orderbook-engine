# orderbook-engine

A low-latency limit order book and matching engine in C++17, built as a
single-threaded matching core and then scaled out to a symbol-sharded
multithreaded design using lock-free queues — no locks anywhere on the
matching hot path.

## Overview

The engine supports price-time priority matching, partial fills, limit and
market orders, and cancels. It's organized in two layers:

- **Matching core** (`OrderBook`) — a single instrument's book: bids/asks
  as sorted price levels, each level a FIFO queue of resting orders.
  Correctness is covered by a dependency-free test suite.
- **Sharded engine** (`ShardedEngine`) — runs many `OrderBook` instances in
  parallel, one per symbol-shard, each pinned to its own thread with zero
  shared mutable state. Order flow reaches each shard through a hand-rolled
  lock-free SPSC ring buffer instead of a mutex.

The two layers are independent: the matching core doesn't know it's being
sharded, and the sharding layer never touches book internals directly.

## Architecture

```
producer thread 0 ──▶ SPSC queue ──▶┐
producer thread 1 ──▶ SPSC queue ──▶┼──▶ Shard 0 thread ──▶ OrderBook(s) for symbol % N == 0
producer thread N ──▶ SPSC queue ──▶┘

                        ... one such fan-in per shard ...
```

Each instrument is routed deterministically (`symbol % numShards`), so a
given symbol's orders always land on the same shard and price-time
ordering within that book holds even though shards run fully in parallel
across symbols. Rather than locking a single shared book — which adds
contention without real parallelism, since matching for one instrument
must stay strictly sequential regardless of thread count — this design
parallelizes *across* instruments instead of trying to parallelize *within*
one.

## Performance

Measured on a dev machine, `-O2`, 4 producer threads, 64 symbols, 2M
synthetic events (90% add / 10% cancel):

| Shards | Throughput | Matching latency p50 | Matching latency p99 |
|--------|------------|----------------------|-----------------------|
| 1      | 3.22M events/s | 0.125 μs | 2.17 μs |
| 4      | 9.67M events/s | 0.167 μs | 2.38 μs |
| 8      | 10.24M events/s | 0.208 μs | 2.96 μs |

Throughput scales close to linearly from 1→4 shards; matching latency
stays essentially flat regardless of shard count, since each shard is
still a contention-free, single-threaded matching loop — sharding adds
parallelism without ever touching the matching hot path. (Scaling flattens
past 4 shards here because the benchmark only uses 4 producer threads —
see `DESIGN_NOTES.md` for the full breakdown and known limitations.)

Always re-run the benchmarks on your own hardware before citing a number —
see below.

## Getting started

```bash
git clone <this-repo-url>
cd orderbook-engine
make            # builds demo, single-threaded bench, tests, sharded bench; runs tests
```

Run each piece:

```bash
./build/demo                              # readable demo of a cross + partial fill
./build/bench 1000000                     # single-threaded throughput/latency, arg = event count
./build/bench_sharded 4 4 64 4000000      # sharded: <shards> <producers> <symbols> <events>
```

Requires a C++17 compiler and `make`. No external dependencies.

## Project layout

```
include/order.hpp          Order/Trade structs (integer prices, in ticks)
include/order_book.hpp     OrderBook interface
src/order_book.cpp         Matching logic (price-time priority, partial fills, cancels)
src/main.cpp               Small demo: a cross and a partial fill
tests/test_order_book.cpp  Correctness tests (assert-based, no framework)
bench/benchmark.cpp        Single-threaded throughput/latency benchmark

include/spsc_queue.hpp     Lock-free bounded SPSC ring buffer
include/affinity.hpp       Best-effort thread/core placement
include/sharded_engine.hpp Shard + ShardedEngine: symbol-sharded matching
include/timing.hpp         Monotonic-clock helper
bench/bench_sharded.cpp    Multi-producer, multi-shard benchmark
```

