# Directories
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
BIN_DIR := bin
SCRIPT_DIR := scripts
RESULTS_DIR := results

# Toolchain
CXX := g++
RM := rm -rf
MKDIR_P := mkdir -p

# PAPI (ajuste se necessário)
PAPI_INC ?= ../papi/install/include
PAPI_LIB ?= ../papi/install/lib

INCLUDES := -I$(SRC_DIR) -I$(INC_DIR) -I$(PAPI_INC)
LIBS := -L$(PAPI_LIB) -lpapi

# Global compile flags for "normal" files
CXXFLAGS := -O3 -march=native -fno-tree-vectorize -std=c++17
LDFLAGS :=

# Source files (list explicitly or discover)
SRC := $(wildcard $(SRC_DIR)/*.cpp)
# Map source -> object in build dir
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC))

# Explicit object names for special flags
AVX_SRC := $(SRC_DIR)/avx_kernel.cpp
SCALAR_SRC := $(SRC_DIR)/scalar_kernel.cpp
AVX_OBJ := $(BUILD_DIR)/avx_kernel.o
SCALAR_OBJ := $(BUILD_DIR)/scalar_kernel.o

# final binary
TARGET := $(BIN_DIR)/matmul_mixed

.PHONY: all clean distclean run dirs

all: dirs $(TARGET)

# ensure directories exist
dirs:
	@$(MKDIR_P) $(BUILD_DIR) $(BIN_DIR) $(RESULTS_DIR)

# Link
$(TARGET): $(OBJS)
	@echo "[LD] $@"
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

# Pattern rule for "normal" sources -> objects
# This will apply to all src/*.cpp except the two special ones (we override them below)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@$(MKDIR_P) $(dir $@)
	@echo "[CXX] $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Special rule for AVX kernel - compile with AVX-512 enabled
$(AVX_OBJ): $(AVX_SRC)
	@$(MKDIR_P) $(dir $@)
	@echo "[CXX,avx512] $< -> $@"
	$(CXX) -O3 -mavx512f -march=native $(INCLUDES) -c $< -o $@

# Special rule for scalar kernel - compile WITHOUT AVX-512
$(SCALAR_OBJ): $(SCALAR_SRC)
	@$(MKDIR_P) $(dir $@)
	@echo "[CXX,scalar] $< -> $@"
	$(CXX) -O3 -mno-avx512f $(INCLUDES) -c $< -o $@

# Ensure OBJS has the special objects replaced (in case wildcard included them)
# (these phony dependencies force the special ones to be built with their rules)
$(BUILD_DIR)/avx_kernel.o: ;
$(BUILD_DIR)/scalar_kernel.o: ;

# Clean build artifacts
clean:
	@echo "Cleaning build and bin (keeping scripts/results)..."
	@$(RM) $(BUILD_DIR)/*
	@$(RM) $(BIN_DIR)/*

# Full clean including results (use with cuidado)
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


