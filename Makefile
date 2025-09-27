# Directories
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
BIN_DIR := bin
RESULTS_DIR := results

# Toolchain
CXX := g++
RM := rm -rf
MKDIR_P := mkdir -p

# PAPI (adjust if necessary)
PAPI_INC ?= /opt/papi/include
PAPI_LIB ?= /opt/papi/lib
LDFLAGS += -Wl,-rpath=$(PAPI_LIB)

INCLUDES := -I$(SRC_DIR) -I$(INC_DIR) -I$(PAPI_INC)
LIBS := -L$(PAPI_LIB) -lpapi

# Global compile flags for "normal" files
CXXFLAGS := -O3 -march=native -fno-tree-vectorize -std=c++17

# Source files
SRC := $(wildcard $(SRC_DIR)/*.cpp)
# Map source -> object in build dir
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC))

# Special object paths
AVX_SRC := $(SRC_DIR)/avx_kernel.cpp
SCALAR_SRC := $(SRC_DIR)/scalar_kernel.cpp
AVX_OBJ := $(BUILD_DIR)/avx_kernel.o
SCALAR_OBJ := $(BUILD_DIR)/scalar_kernel.o

# final binary
TARGET := $(BIN_DIR)/matmul_mixed

.PHONY: all clean distclean run dirs show

all: dirs $(TARGET)

# ensure directories exist
dirs:
	@$(MKDIR_P) $(BUILD_DIR) $(BIN_DIR) $(RESULTS_DIR)

# Link
$(TARGET): $(OBJS)
	@echo "[LD] $@"
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

# Generic rule for normal sources -> objects
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@$(MKDIR_P) $(dir $@)
	@echo "[CXX] $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Special rule for AVX kernel - compile with avx512 support
$(AVX_OBJ): $(AVX_SRC)
	@$(MKDIR_P) $(dir $@)
	@echo "[CXX,avx512] $< -> $@"
	$(CXX) -O3 -mavx512f -march=native $(INCLUDES) -c $< -o $@

# Special rule for scalar kernel - compile WITHOUT avx512
$(SCALAR_OBJ): $(SCALAR_SRC)
	@$(MKDIR_P) $(dir $@)
	@echo "[CXX,scalar] $< -> $@"
	$(CXX) -O3 -mno-avx512f $(INCLUDES) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build and bin (keeping scripts/results)..."
	@$(RM) $(BUILD_DIR)/*
	@$(RM) $(BIN_DIR)/*

# Full clean including results
distclean: clean
	@echo "Removing results directory as well..."
	@$(RM) $(RESULTS_DIR)/*

# Convenience: run the produced binary on CPU core 0 with example args
# Usage: make run ARGS="2048 128 mixed 12345"
run: all
	@if [ -z "$(ARGS)" ]; then \
	  echo "Usage: make run ARGS=\"N BS mode seed\" (e.g. ARGS=\"2048 128 mixed 12345\")"; \
	else \
	  taskset -c 0 $(TARGET) $(ARGS); \
	fi

# show variables for debugging
show:
	@echo SRC_DIR=$(SRC_DIR)
	@echo BUILD_DIR=$(BUILD_DIR)
	@echo BIN_DIR=$(BIN_DIR)
	@echo SRC=$(SRC)
	@echo OBJS=$(OBJS)

