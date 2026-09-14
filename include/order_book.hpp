#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "order.hpp"

// ---------------------------------------------------------------------------
// Matching core.
//
// Price levels are flat arrays indexed directly by price-in-ticks (a
// bounded, configurable range), so best-price access and level lookup are
// O(1) instead of the O(log P) tree lookup a std::map gives you. Order
// books have a bounded, near-contiguous set of active price ticks around
// the touch, so a flat array beats a balanced tree here -- the cost is a
// fixed price range you have to size up front (see maxPriceTicks below).
//
// Resting orders at each level live in an intrusive doubly-linked list
// (the prev/next pointers live inside the Node itself), backed by a
// fixed-capacity object pool (a preallocated arena + freelist). Cancel is
// an O(1) unlink given a direct node pointer -- stored in the id->Node*
// map -- instead of a linear scan through the level, and there is no
// per-order heap allocation once the pool is warmed up.
//
// See DESIGN_NOTES.md for the before/after numbers, the reasoning behind
// each choice, and the tradeoffs this design accepts (fixed price range,
// fixed pool capacity, a worst-case O(range) scan when the best level
// empties with no nearby occupied levels).
// ---------------------------------------------------------------------------
class OrderBook {
public:
    // Valid order prices are [0, maxPriceTicks]; maxRestingOrders bounds
    // how many orders can rest on the book at once (pool capacity). Both
    // are fixed at construction -- pre-allocating everything up front
    // instead of growing on the hot path is a deliberate low-latency
    // design choice, not an oversight.
    explicit OrderBook(int64_t maxPriceTicks = 20000, std::size_t maxRestingOrders = 50000);
    ~OrderBook();

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // Matches the incoming order against the book, returns any resulting
    // trades, and rests whatever quantity is left (limit orders only).
    std::vector<Trade> addOrder(Order order);

    // Removes a resting order by id. Returns true if it was found.
    bool cancelOrder(uint64_t orderId);

    bool bestBid(int64_t& priceOut) const;
    bool bestAsk(int64_t& priceOut) const;

    std::size_t bidLevelCount() const { return bidLevelCount_; }
    std::size_t askLevelCount() const { return askLevelCount_; }

private:
    struct Node {
        Order order;
        Node* prev = nullptr;
        Node* next = nullptr;
        int64_t priceIdx = -1;
        Side side{};
    };
    struct Level {
        Node* head = nullptr;
        Node* tail = nullptr;
    };

    std::vector<Node> arena_;
    std::vector<Node*> freeList_;
    Node* acquireNode();
    void releaseNode(Node* node);

    std::vector<Level> bidLevels_;
    std::vector<Level> askLevels_;
    int64_t maxPriceTicks_;
    int64_t bestBidIdx_ = -1;   // -1 means "no bids resting"
    int64_t bestAskIdx_ = -1;   // -1 means "no asks resting"
    std::size_t bidLevelCount_ = 0;
    std::size_t askLevelCount_ = 0;

    std::unordered_map<uint64_t, Node*> locations_;

    static void appendToLevel(Level& lvl, Node* node);
    static void unlinkFromLevel(Level& lvl, Node* node);
    void advanceBestBidFrom(int64_t startIdx);
    void advanceBestAskFrom(int64_t startIdx);

    std::vector<Trade> match(Order& incoming);
};
