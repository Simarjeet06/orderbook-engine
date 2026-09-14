#pragma once
#include <map>
#include <deque>
#include <unordered_map>
#include <vector>
#include "order.hpp"

// ---------------------------------------------------------------------------
// NAIVE BASELINE IMPLEMENTATION -- this is the thing you're meant to optimize.
//
// It is correct (price-time priority, partial fills, cancels) but makes the
// simplest possible choice at every turn:
//   - price levels live in a std::map (red-black tree)   -> O(log P) per touch
//   - each level is a std::deque<Order>                  -> node allocations
//   - cancel does a linear scan within its price level    -> O(n) per cancel
//   - order id -> location lookup is a std::unordered_map -> extra indirection
// None of this is wrong. It's just not fast. See README.md for a concrete
// list of what to change and why, roughly in order of expected payoff.
// ---------------------------------------------------------------------------
class OrderBook {
public:
    // Matches the incoming order against the book, returns any resulting
    // trades, and rests whatever quantity is left (limit orders only).
    std::vector<Trade> addOrder(Order order);

    // Removes a resting order by id. Returns true if it was found.
    bool cancelOrder(uint64_t orderId);

    bool bestBid(int64_t& priceOut) const;
    bool bestAsk(int64_t& priceOut) const;

    size_t bidLevelCount() const { return bids_.size(); }
    size_t askLevelCount() const { return asks_.size(); }

private:
    // Bids sorted highest-first, asks sorted lowest-first, so begin() is
    // always the best price on each side.
    std::map<int64_t, std::deque<Order>, std::greater<int64_t>> bids_;
    std::map<int64_t, std::deque<Order>> asks_;

    struct Location { Side side; int64_t price; };
    std::unordered_map<uint64_t, Location> locations_;

    std::vector<Trade> match(Order& incoming);
};
