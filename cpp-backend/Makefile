# MOM'S MAGIC - C++ backend (Linux / POSIX)
#
#   make            build release binary ./bin/server
#   make debug      build with -O0 -g -fsanitize=address,undefined
#   make test       build + run the smoke test (needs curl)
#   make clean      remove build artifacts

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
LDFLAGS  ?= -pthread

SRC_DIR := src
BUILD   := build
BIN     := bin

SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD)/%.o,$(SOURCES))
DEPS    := $(OBJECTS:.o=.d)

TARGET  := $(BIN)/server

.PHONY: all debug run test clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

debug: CXXFLAGS := -std=c++17 -O0 -g -Wall -Wextra -pthread -fsanitize=address,undefined
debug: clean
debug: $(TARGET)

run: $(TARGET)
	./$(TARGET) --port 5000

test: $(TARGET)
	@bash test/smoke.sh 5010

clean:
	rm -rf $(BUILD) $(BIN)
