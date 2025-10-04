# Makefile for packed matmul project (AVX512 + scalar_sensitive)
CXX := g++
CXXFLAGS := -O3 -march=native -fno-tree-vectorize -std=c++17
LDFLAGS :=
PAPI_INC ?= /opt/papi/include/
PAPI_LIB ?= /opt/papi/lib
INC_DIR = include
INCLUDES := -I. -Isrc -I$(INC_DIR) -I$(PAPI_INC)
LIBS := -L$(PAPI_LIB) -lpapi

BUILD_DIR := build
SRC_DIR := src
BIN_DIR := bin

EXTRA ?= 0
DEFINES := -DEXTRA_AVX_HEAT_REPS=$(EXTRA)

SRC := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC))

TARGET := $(BIN_DIR)/matmul_mixed

.PHONY: all clean dirs

all: dirs $(TARGET) $(BIN_DIR)/heater_avx

dirs:
	mkdir -p $(BUILD_DIR) $(BIN_DIR) results scripts

# default rule for normal sources (compiled with CXXFLAGS)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX] $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# override compile flags for avx_kernel (enable avx512 and EXTRA define)
$(BUILD_DIR)/avx_kernel.o: $(SRC_DIR)/avx_kernel.cpp
	@echo "[CXX,avx512] $< -> $@"
	$(CXX) -O3 -mavx512f -march=native $(DEFINES) $(INCLUDES) -c $< -o $@

# override compile flags for scalar kernel: disable avx512 and disable auto-vectorize
$(BUILD_DIR)/scalar_kernel.o: $(SRC_DIR)/scalar_kernel.cpp
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
$(TARGET): $(BUILD_DIR)/main.o $(BUILD_DIR)/avx_kernel.o $(BUILD_DIR)/scalar_kernel.o $(BUILD_DIR)/papito.o
	@echo "[LD] $@"
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

clean:
	rm -rf $(BUILD_DIR)/* $(BIN_DIR)/* results/*


