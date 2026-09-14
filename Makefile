CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude

BUILD_DIR := build

.PHONY: all demo bench test clean

all: demo bench test

demo: $(BUILD_DIR)/demo
bench: $(BUILD_DIR)/bench
test: $(BUILD_DIR)/test
	$(BUILD_DIR)/test

$(BUILD_DIR)/demo: src/order_book.cpp src/main.cpp include/order_book.hpp include/order.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/order_book.cpp src/main.cpp -o $@

$(BUILD_DIR)/bench: src/order_book.cpp bench/benchmark.cpp include/order_book.hpp include/order.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/order_book.cpp bench/benchmark.cpp -o $@

$(BUILD_DIR)/test: src/order_book.cpp tests/test_order_book.cpp include/order_book.hpp include/order.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/order_book.cpp tests/test_order_book.cpp -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
