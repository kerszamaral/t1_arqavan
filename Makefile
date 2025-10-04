# Makefile for packed matmul project (AVX512 + scalar_sensitive)
CXX := g++
CXXFLAGS := -O3 -march=native -fno-tree-vectorize -std=c++17
LDFLAGS :=
INC_DIR = include
INCLUDES := -I. -Isrc -I$(INC_DIR)
LIBS := -lpapi

BUILD_DIR := build
SRC_DIR := src
BIN_DIR := bin

HYBRID_AVX_UNROLL ?= 1
HYBRID_SCALAR_UNROLL ?= 2
INTERLEAVED_AVX_OPS ?= 1
INTERLEAVED_SCALAR_OPS ?= 1

HYBRID_DEFINES := -DHYBRID_AVX_UNROLL=$(HYBRID_AVX_UNROLL) -DHYBRID_SCALAR_UNROLL=$(HYBRID_SCALAR_UNROLL)
INTERLEAVED_DEFINES := -DINTERLEAVED_AVX_OPS=$(INTERLEAVED_AVX_OPS) -DINTERLEAVED_SCALAR_OPS=$(INTERLEAVED_SCALAR_OPS)

SRC := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC))

TARGET := $(BIN_DIR)/matmul_mixed

.PHONY: all clean dirs lint

all: dirs $(TARGET) $(BIN_DIR)/heater_avx

# New rule to generate compile_commands.json
lint:
	@echo "[LINT] Generating compile_commands.json with bear..."
	bear -- make all

dirs:
	mkdir -p $(BUILD_DIR) $(BIN_DIR) results scripts

# default rule for normal sources (compiled with CXXFLAGS)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX] $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Update rules for the renamed kernel files
$(BUILD_DIR)/kernel_avx.o: $(SRC_DIR)/kernel_avx.cpp
	@echo "[CXX,avx512] $< -> $@"
	$(CXX) -O3 -mavx512f -march=native $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/kernel_hybrid.o: $(SRC_DIR)/kernel_hybrid.cpp
	@echo "[CXX,avx512,hybrid] $< -> $@"
	$(CXX) -O3 -mavx512f -march=native $(HYBRID_DEFINES) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/kernel_interleaved.o: $(SRC_DIR)/kernel_interleaved.cpp
	@echo "[CXX,avx512,interleaved] $< -> $@"
	$(CXX) -O3 -mavx512f -march=native $(INTERLEAVED_DEFINES) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/kernel_scalar.o: $(SRC_DIR)/kernel_scalar.cpp
	@echo "[CXX,scalar] $< -> $@"
	$(CXX) -O3 -mno-avx512f -fno-tree-vectorize $(INCLUDES) -c $< -o $@

# special: make sure papito is compiled normally (if present)
$(BUILD_DIR)/papito.o: $(SRC_DIR)/papito.cpp $(INC_DIR)/papito.h
	@echo "[CXX] papito"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $(SRC_DIR)/papito.cpp -o $(BUILD_DIR)/papito.o

# build heater (AVX-512)
$(BUILD_DIR)/heater_avx.o: $(SRC_DIR)/heater_avx.c
	@mkdir -p $(dir $@)
	@echo "[C] heater_avx"
	$(CXX) -O3 -mavx512f -march=native -fno-tree-vectorize -c $(SRC_DIR)/heater_avx.c -o $(BUILD_DIR)/heater_avx.o

# link heater into bin/heater_avx
$(BIN_DIR)/heater_avx: $(BUILD_DIR)/heater_avx.o
	@mkdir -p $(BIN_DIR)
	@echo "[LD] heater_avx"
	$(CXX) $(CFLAGS) $(BUILD_DIR)/heater_avx.o -o $(BIN_DIR)/heater_avx

# Link target
$(TARGET): $(OBJS)
	@echo $(OBJS)
	@echo $(SRC)
	@echo "[LD] $@"
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

clean:
	rm -rf $(BUILD_DIR)/* $(BIN_DIR)/* results/*
