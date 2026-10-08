# Animation sandbox - portable build (no Windows toolchain required).
# Usage:  make          (build)
#         make test     (build + run invariant tests)
#         make demo     (build + run the scripted trace)

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
SRC       = src/anim_sim.cpp src/main.cpp
HDR       = src/anim_sim.hpp
OUT       = build/anim_sandbox

all: $(OUT)

$(OUT): $(SRC) $(HDR)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -o $(OUT) $(SRC)

test: $(OUT)
	./$(OUT) test

demo: $(OUT)
	./$(OUT) demo

clean:
	rm -rf build

.PHONY: all test demo clean
