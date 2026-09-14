#pragma once
#include <cstdint>

enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : uint8_t { Limit = 0, Market = 1 };

struct Order {
    uint64_t id;
    Side side;
    OrderType type;
    int64_t price;      // integer ticks, not floating point -- keeps the hot path exact & fast
    uint32_t quantity;
    uint64_t timestamp;  // monotonic sequence number, used for time priority
};

struct Trade {
    uint64_t restingOrderId;
    uint64_t incomingOrderId;
    int64_t price;
    uint32_t quantity;
    uint64_t timestamp;
};
