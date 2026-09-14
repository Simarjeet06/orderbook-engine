CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude
THREAD_FLAGS := -pthread

BUILD_DIR := build

.PHONY: all demo bench test bench_sharded clean

all: demo bench test bench_sharded

demo: $(BUILD_DIR)/demo
bench: $(BUILD_DIR)/bench
test: $(BUILD_DIR)/test
	$(BUILD_DIR)/test
bench_sharded: $(BUILD_DIR)/bench_sharded

$(BUILD_DIR)/demo: src/order_book.cpp src/main.cpp include/order_book.hpp include/order.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/order_book.cpp src/main.cpp -o $@

$(BUILD_DIR)/bench: src/order_book.cpp bench/benchmark.cpp include/order_book.hpp include/order.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/order_book.cpp bench/benchmark.cpp -o $@

$(BUILD_DIR)/test: src/order_book.cpp tests/test_order_book.cpp include/order_book.hpp include/order.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/order_book.cpp tests/test_order_book.cpp -o $@

$(BUILD_DIR)/bench_sharded: src/order_book.cpp bench/bench_sharded.cpp include/order_book.hpp include/order.hpp include/sharded_engine.hpp include/spsc_queue.hpp include/affinity.hpp include/timing.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(THREAD_FLAGS) src/order_book.cpp bench/bench_sharded.cpp -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
