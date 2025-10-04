#pragma once

#include "kernels.h"
#include <string>

matmul_func_t get_kernel_for_mode(const std::string& mode);

