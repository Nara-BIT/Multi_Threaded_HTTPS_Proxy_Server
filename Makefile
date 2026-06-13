CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Iinclude
LDFLAGS  = -lpthread

SRCS    = $(wildcard src/*.cpp)
OBJS    = $(patsubst src/%.cpp, build/%.o, $(SRCS))
TARGET  = proxy

.PHONY: all clean run

all: build $(TARGET)

build:
	@mkdir -p build

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)
	@echo "\n✓ Build successful → ./$(TARGET)\n"

build/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	./$(TARGET) config.txt

clean:
	rm -rf build $(TARGET)

# Quick smoke test — requires proxy to be running
test:
	@echo "--- Testing HTTP ---"
	curl -sx http://localhost:8080 http://httpbin.org/get | head -5
	@echo "\n--- Testing cache (second request should be HIT) ---"
	curl -sx http://localhost:8080 http://httpbin.org/get | head -5
	@echo "\n--- Concurrent load (10 parallel requests) ---"
	@for i in $$(seq 1 10); do curl -sx http://localhost:8080 http://httpbin.org/ip & done; wait
	@echo "\n--- HTTPS tunnel ---"
	curl -sx http://localhost:8080 https://httpbin.org/get | head -5
