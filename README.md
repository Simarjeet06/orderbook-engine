# orderbook-engine

A deliberately **naive** C++ limit order book / matching engine. It is
correct — price-time priority, partial fills, limit + market orders,
cancels — but built with the simplest data structures possible. The point
of this repo is not the code as it stands; it's what you replace it with.

## What's here

```
include/order.hpp        Order/Trade structs (plain, integer prices in ticks)
include/order_book.hpp   OrderBook interface
src/order_book.cpp       The naive matching logic
src/main.cpp             Tiny readable demo of a cross + partial fill
bench/benchmark.cpp      Synthetic order flow + throughput/latency harness
tests/test_order_book.cpp  Dependency-free correctness tests (assert-based)
```

## Build & run

```
make          # builds demo, bench, test; runs the test suite
./build/demo
./build/bench 1000000     # arg = number of synthetic events, default 1M
```

## Baseline numbers

Measured with `-O2` in this repo's dev sandbox (a shared cloud container —
not dedicated hardware, so treat these as a shape, not a spec). Re-run
`./build/bench` on whatever machine you're actually optimizing for and use
that as your real baseline:

```
events:        1,000,000
wall time:     481 ms
throughput:    ~2.08M events/sec
latency p50:   0.22 us
latency p99:   3.86 us
latency p999:  9.25 us
latency max:   6835 us   <- one big outlier, worth investigating (see below)
```

That max is the first interesting thing to chase: a single event taking
6.8ms next to a p999 of 9us is almost certainly an STL container doing a
large reallocation (the `std::vector<Trade>` growing, or a `std::map`
rebalance) rather than anything about the matching logic itself. Profiling
*that* is a good first exercise before changing any code.

## What's naive about it, roughly in order of expected payoff

1. **Price levels are a `std::map`.** Every touch of the best price is an
   `O(log P)` tree lookup, and inserting a brand-new price level allocates
   a tree node. Order books have a bounded, mostly-contiguous set of price
   ticks — replace this with a flat array/vector indexed by tick (or a
   sparse variant if the tick range is huge), so best-price access is
   `O(1)` and level insertion doesn't allocate.

2. **Cancel is a linear scan within a price level.** `OrderBook::cancelOrder`
   walks the `std::deque` to find the order. Swap the deque for an
   intrusive doubly-linked list (the prev/next pointers live inside `Order`
   itself), and have the id->location map store a direct node pointer
   instead of just a price — cancel becomes `O(1)`.

3. **Every resting order triggers STL node allocation.** `std::map` and
   `std::deque` both allocate per-element under the hood. Add an object
   pool / arena allocator for `Order` nodes (fixed-size freelist, since
   orders are POD-ish and same-sized) and this mostly disappears.

4. **`match()` returns `std::vector<Trade>` by value on every call.**
   Fine at 1M events/sec, less fine as you push throughput up. Consider an
   output parameter, a caller-owned scratch buffer, or a small-vector
   optimization for the common case of 1-2 trades per incoming order.

5. **Single-threaded, synchronous, no separation between ingestion and
   matching.** A real low-latency design puts a lock-free SPSC ring buffer
   between the feed handler (parsing incoming order messages) and the
   matching thread, so matching never blocks on I/O or parsing. That's a
   natural "phase 2" on top of this repo rather than a change to
   `OrderBook` itself.

6. **No latency histogram, just percentiles from a sorted vector.** Good
   enough for offline benchmarking; if you want live p99 tracking during a
   run, look at HdrHistogram-style fixed-bucket histograms instead of
   storing every sample.

7. **Compiler flags are free wins you haven't taken yet.** Try `-O3`,
   `-march=native`, and profile-guided optimization before touching data
   structures at all — it's a five-minute experiment and worth having the
   before/after number.

## Suggested workflow

Keep `tests/test_order_book.cpp` green through every change — it's your
proof that an "optimized" version still matches the same trades in the
same order. Benchmark before and after each change individually (not all
five at once) so you have a defensible number for each decision — that's
the part that's actually worth having in an interview: not "I made it
faster" but "replacing the deque-based cancel with an intrusive list took
cancel latency from X to Y, here's the profile that told me to look there."

## Explicitly out of scope here

No risk checks, no persistence/WAL, no market data dissemination, no ITCH/
FIX parsing. Those are real parts of a production matching engine but
separate concerns from the matching logic itself — worth building as
follow-on pieces once this core is fast, not folded into it.
