#include "order_book.hpp"
#include <algorithm>
#include <stdexcept>

OrderBook::OrderBook(int64_t maxPriceTicks, std::size_t maxRestingOrders)
    : arena_(maxRestingOrders),
      bidLevels_(static_cast<std::size_t>(maxPriceTicks) + 1),
      askLevels_(static_cast<std::size_t>(maxPriceTicks) + 1),
      maxPriceTicks_(maxPriceTicks) {
    freeList_.reserve(maxRestingOrders);
    for (std::size_t i = maxRestingOrders; i-- > 0;) {
        freeList_.push_back(&arena_[i]);
    }
}

OrderBook::~OrderBook() = default;

OrderBook::Node* OrderBook::acquireNode() {
    if (freeList_.empty()) {
        throw std::runtime_error(
            "OrderBook: resting-order pool exhausted; construct with a larger maxRestingOrders");
    }
    Node* n = freeList_.back();
    freeList_.pop_back();
    return n;
}

void OrderBook::releaseNode(Node* node) { freeList_.push_back(node); }

void OrderBook::appendToLevel(Level& lvl, Node* node) {
    node->prev = lvl.tail;
    node->next = nullptr;
    if (lvl.tail) lvl.tail->next = node; else lvl.head = node;
    lvl.tail = node;
}

void OrderBook::unlinkFromLevel(Level& lvl, Node* node) {
    if (node->prev) node->prev->next = node->next; else lvl.head = node->next;
    if (node->next) node->next->prev = node->prev; else lvl.tail = node->prev;
    node->prev = node->next = nullptr;
}

void OrderBook::advanceBestBidFrom(int64_t startIdx) {
    for (int64_t i = startIdx; i >= 0; --i) {
        if (bidLevels_[static_cast<std::size_t>(i)].head != nullptr) {
            bestBidIdx_ = i;
            return;
        }
    }
    bestBidIdx_ = -1;
}

void OrderBook::advanceBestAskFrom(int64_t startIdx) {
    for (int64_t i = startIdx; i <= maxPriceTicks_; ++i) {
        if (askLevels_[static_cast<std::size_t>(i)].head != nullptr) {
            bestAskIdx_ = i;
            return;
        }
    }
    bestAskIdx_ = -1;
}

std::vector<Trade> OrderBook::addOrder(Order order) {
    if (order.price < 0 || order.price > maxPriceTicks_) {
        throw std::out_of_range("OrderBook: order price outside configured [0, maxPriceTicks] range");
    }

    std::vector<Trade> trades = match(order);

    if (order.quantity > 0 && order.type == OrderType::Limit) {
        Node* node = acquireNode();
        node->order = order;
        node->priceIdx = order.price;
        node->side = order.side;
        node->prev = node->next = nullptr;

        if (order.side == Side::Buy) {
            Level& lvl = bidLevels_[static_cast<std::size_t>(order.price)];
            const bool wasEmpty = (lvl.head == nullptr);
            appendToLevel(lvl, node);
            if (wasEmpty) {
                ++bidLevelCount_;
                if (bestBidIdx_ == -1 || order.price > bestBidIdx_) bestBidIdx_ = order.price;
            }
        } else {
            Level& lvl = askLevels_[static_cast<std::size_t>(order.price)];
            const bool wasEmpty = (lvl.head == nullptr);
            appendToLevel(lvl, node);
            if (wasEmpty) {
                ++askLevelCount_;
                if (bestAskIdx_ == -1 || order.price < bestAskIdx_) bestAskIdx_ = order.price;
            }
        }
        locations_[order.id] = node;
    }
    // Market orders that can't be fully filled just die here -- no resting
    // market orders. That's a deliberate simplification, not a bug.

    return trades;
}

std::vector<Trade> OrderBook::match(Order& incoming) {
    std::vector<Trade> trades;

    if (incoming.side == Side::Buy) {
        while (incoming.quantity > 0 && bestAskIdx_ != -1) {
            if (incoming.type == OrderType::Limit && incoming.price < bestAskIdx_) break;

            Level& lvl = askLevels_[static_cast<std::size_t>(bestAskIdx_)];
            const int64_t levelPrice = bestAskIdx_;
            while (incoming.quantity > 0 && lvl.head != nullptr) {
                Node* resting = lvl.head;
                const uint32_t tradedQty = std::min(incoming.quantity, resting->order.quantity);

                trades.push_back(Trade{resting->order.id, incoming.id, levelPrice, tradedQty, incoming.timestamp});

                incoming.quantity -= tradedQty;
                resting->order.quantity -= tradedQty;

                if (resting->order.quantity == 0) {
                    locations_.erase(resting->order.id);
                    unlinkFromLevel(lvl, resting);
                    releaseNode(resting);
                }
            }
            if (lvl.head == nullptr) {
                --askLevelCount_;
                advanceBestAskFrom(bestAskIdx_ + 1);
            }
        }
    } else {
        while (incoming.quantity > 0 && bestBidIdx_ != -1) {
            if (incoming.type == OrderType::Limit && incoming.price > bestBidIdx_) break;

            Level& lvl = bidLevels_[static_cast<std::size_t>(bestBidIdx_)];
            const int64_t levelPrice = bestBidIdx_;
            while (incoming.quantity > 0 && lvl.head != nullptr) {
                Node* resting = lvl.head;
                const uint32_t tradedQty = std::min(incoming.quantity, resting->order.quantity);

                trades.push_back(Trade{resting->order.id, incoming.id, levelPrice, tradedQty, incoming.timestamp});

                incoming.quantity -= tradedQty;
                resting->order.quantity -= tradedQty;

                if (resting->order.quantity == 0) {
                    locations_.erase(resting->order.id);
                    unlinkFromLevel(lvl, resting);
                    releaseNode(resting);
                }
            }
            if (lvl.head == nullptr) {
                --bidLevelCount_;
                advanceBestBidFrom(bestBidIdx_ - 1);
            }
        }
    }

    return trades;
}

bool OrderBook::cancelOrder(uint64_t orderId) {
    auto it = locations_.find(orderId);
    if (it == locations_.end()) return false;

    Node* node = it->second;
    const int64_t idx = node->priceIdx;

    if (node->side == Side::Buy) {
        Level& lvl = bidLevels_[static_cast<std::size_t>(idx)];
        unlinkFromLevel(lvl, node);
        if (lvl.head == nullptr) {
            --bidLevelCount_;
            if (bestBidIdx_ == idx) advanceBestBidFrom(idx - 1);
        }
    } else {
        Level& lvl = askLevels_[static_cast<std::size_t>(idx)];
        unlinkFromLevel(lvl, node);
        if (lvl.head == nullptr) {
            --askLevelCount_;
            if (bestAskIdx_ == idx) advanceBestAskFrom(idx + 1);
        }
    }

    locations_.erase(it);
    releaseNode(node);
    return true;
}

bool OrderBook::bestBid(int64_t& out) const {
    if (bestBidIdx_ == -1) return false;
    out = bestBidIdx_;
    return true;
}

bool OrderBook::bestAsk(int64_t& out) const {
    if (bestAskIdx_ == -1) return false;
    out = bestAskIdx_;
    return true;
}
