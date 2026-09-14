#include "order_book.hpp"
#include <algorithm>

std::vector<Trade> OrderBook::addOrder(Order order) {
    std::vector<Trade> trades = match(order);

    if (order.quantity > 0 && order.type == OrderType::Limit) {
        if (order.side == Side::Buy) {
            bids_[order.price].push_back(order);
        } else {
            asks_[order.price].push_back(order);
        }
        locations_[order.id] = Location{order.side, order.price};
    }
    // Market orders that can't be fully filled just die here -- no resting
    // market orders. That's a deliberate simplification, not a bug.

    return trades;
}

std::vector<Trade> OrderBook::match(Order& incoming) {
    std::vector<Trade> trades;

    if (incoming.side == Side::Buy) {
        while (incoming.quantity > 0 && !asks_.empty()) {
            auto bestIt = asks_.begin();
            const int64_t bestPrice = bestIt->first;
            if (incoming.type == OrderType::Limit && incoming.price < bestPrice) break;

            auto& queue = bestIt->second;
            while (incoming.quantity > 0 && !queue.empty()) {
                Order& resting = queue.front();
                const uint32_t tradedQty = std::min(incoming.quantity, resting.quantity);

                trades.push_back(Trade{resting.id, incoming.id, bestPrice, tradedQty, incoming.timestamp});

                incoming.quantity -= tradedQty;
                resting.quantity -= tradedQty;

                if (resting.quantity == 0) {
                    locations_.erase(resting.id);
                    queue.pop_front();
                }
            }
            if (queue.empty()) asks_.erase(bestIt);
        }
    } else {
        while (incoming.quantity > 0 && !bids_.empty()) {
            auto bestIt = bids_.begin();
            const int64_t bestPrice = bestIt->first;
            if (incoming.type == OrderType::Limit && incoming.price > bestPrice) break;

            auto& queue = bestIt->second;
            while (incoming.quantity > 0 && !queue.empty()) {
                Order& resting = queue.front();
                const uint32_t tradedQty = std::min(incoming.quantity, resting.quantity);

                trades.push_back(Trade{resting.id, incoming.id, bestPrice, tradedQty, incoming.timestamp});

                incoming.quantity -= tradedQty;
                resting.quantity -= tradedQty;

                if (resting.quantity == 0) {
                    locations_.erase(resting.id);
                    queue.pop_front();
                }
            }
            if (queue.empty()) bids_.erase(bestIt);
        }
    }

    return trades;
}

bool OrderBook::cancelOrder(uint64_t orderId) {
    auto it = locations_.find(orderId);
    if (it == locations_.end()) return false;

    const Location loc = it->second;
    auto eraseFrom = [&](auto& book) {
        auto lvlIt = book.find(loc.price);
        if (lvlIt == book.end()) return false;
        auto& dq = lvlIt->second;
        // Linear scan within the price level -- the obvious first thing to
        // fix once you swap the deque for an intrusive list (O(1) erase
        // given a node pointer instead of an id).
        for (auto qIt = dq.begin(); qIt != dq.end(); ++qIt) {
            if (qIt->id == orderId) {
                dq.erase(qIt);
                break;
            }
        }
        if (dq.empty()) book.erase(lvlIt);
        return true;
    };

    if (loc.side == Side::Buy) eraseFrom(bids_);
    else eraseFrom(asks_);

    locations_.erase(it);
    return true;
}

bool OrderBook::bestBid(int64_t& out) const {
    if (bids_.empty()) return false;
    out = bids_.begin()->first;
    return true;
}

bool OrderBook::bestAsk(int64_t& out) const {
    if (asks_.empty()) return false;
    out = asks_.begin()->first;
    return true;
}
