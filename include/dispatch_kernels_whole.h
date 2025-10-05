#pragma once

#include "kernels_whole.h"
#include <string>

matmul_whole_func_t get_kernel_for_mode_whole(const std::string& mode);

