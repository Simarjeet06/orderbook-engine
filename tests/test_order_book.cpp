#include "order_book.hpp"
#include <cassert>
#include <iostream>

// Dependency-free sanity tests (no gtest/catch2, just assert()).
// Run via `make test`. Add more of these as you refactor -- they're your
// safety net for proving an "optimized" version still behaves identically.

static void test_no_cross_rests_both_sides() {
    OrderBook book;
    auto t1 = book.addOrder({1, Side::Buy, OrderType::Limit, 99, 10, 1});
    auto t2 = book.addOrder({2, Side::Sell, OrderType::Limit, 101, 10, 2});
    assert(t1.empty());
    assert(t2.empty());

    int64_t bid, ask;
    assert(book.bestBid(bid) && bid == 99);
    assert(book.bestAsk(ask) && ask == 101);
}

static void test_exact_cross_fully_fills_both() {
    OrderBook book;
    book.addOrder({1, Side::Sell, OrderType::Limit, 100, 10, 1});
    auto trades = book.addOrder({2, Side::Buy, OrderType::Limit, 100, 10, 2});

    assert(trades.size() == 1);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 10);
    assert(trades[0].restingOrderId == 1);
    assert(trades[0].incomingOrderId == 2);

    int64_t dummy;
    assert(!book.bestBid(dummy));
    assert(!book.bestAsk(dummy));
}

static void test_partial_fill_leaves_remainder_resting() {
    OrderBook book;
    book.addOrder({1, Side::Sell, OrderType::Limit, 100, 5, 1});
    auto trades = book.addOrder({2, Side::Buy, OrderType::Limit, 100, 12, 2});

    assert(trades.size() == 1);
    assert(trades[0].quantity == 5);

    int64_t bid;
    assert(book.bestBid(bid) && bid == 100); // remaining 7 rests
}

static void test_price_time_priority() {
    OrderBook book;
    // Two sells at the same price; the earlier one (id=1) must fill first.
    book.addOrder({1, Side::Sell, OrderType::Limit, 100, 5, 1});
    book.addOrder({2, Side::Sell, OrderType::Limit, 100, 5, 2});

    auto trades = book.addOrder({3, Side::Buy, OrderType::Limit, 100, 5, 3});
    assert(trades.size() == 1);
    assert(trades[0].restingOrderId == 1);
}

static void test_sweeps_best_price_first() {
    OrderBook book;
    book.addOrder({1, Side::Sell, OrderType::Limit, 100, 5, 1});
    book.addOrder({2, Side::Sell, OrderType::Limit, 101, 5, 2});

    auto trades = book.addOrder({3, Side::Buy, OrderType::Limit, 101, 10, 3});
    assert(trades.size() == 2);
    assert(trades[0].price == 100);
    assert(trades[1].price == 101);
}

static void test_cancel_removes_resting_order() {
    OrderBook book;
    book.addOrder({1, Side::Buy, OrderType::Limit, 99, 10, 1});
    assert(book.cancelOrder(1));
    assert(!book.cancelOrder(1)); // already gone

    int64_t dummy;
    assert(!book.bestBid(dummy));
}

static void test_market_order_consumes_best_price() {
    OrderBook book;
    book.addOrder({1, Side::Sell, OrderType::Limit, 100, 5, 1});
    book.addOrder({2, Side::Sell, OrderType::Limit, 101, 5, 2});

    auto trades = book.addOrder({3, Side::Buy, OrderType::Market, 0, 5, 3});
    assert(trades.size() == 1);
    assert(trades[0].price == 100); // market order takes the best price, ignores its own price field
}

int main() {
    test_no_cross_rests_both_sides();
    test_exact_cross_fully_fills_both();
    test_partial_fill_leaves_remainder_resting();
    test_price_time_priority();
    test_sweeps_best_price_first();
    test_cancel_removes_resting_order();
    test_market_order_consumes_best_price();

    std::cout << "All tests passed.\n";
    return 0;
}
