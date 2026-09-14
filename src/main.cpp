#include "order_book.hpp"
#include <iostream>

// Small, readable demo of the book in action. Not a benchmark --
// see bench/benchmark.cpp for that.
int main() {
    OrderBook book;

    auto printTrades = [](const std::vector<Trade>& trades) {
        for (const auto& t : trades) {
            std::cout << "  TRADE resting=" << t.restingOrderId
                      << " incoming=" << t.incomingOrderId
                      << " price=" << t.price
                      << " qty=" << t.quantity << "\n";
        }
    };

    std::cout << "Resting a sell 10 @ 101\n";
    printTrades(book.addOrder({1, Side::Sell, OrderType::Limit, 101, 10, 1}));

    std::cout << "Resting a sell 5 @ 100\n";
    printTrades(book.addOrder({2, Side::Sell, OrderType::Limit, 100, 5, 2}));

    std::cout << "Buy 12 @ 101 (should sweep the 100 level first, then partially fill 101)\n";
    printTrades(book.addOrder({3, Side::Buy, OrderType::Limit, 101, 12, 3}));

    int64_t bestAsk;
    if (book.bestAsk(bestAsk)) {
        std::cout << "Best ask remaining: " << bestAsk << "\n";
    }

    std::cout << "Cancelling order 1\n";
    std::cout << "  cancelled=" << std::boolalpha << book.cancelOrder(1) << "\n";

    return 0;
}
