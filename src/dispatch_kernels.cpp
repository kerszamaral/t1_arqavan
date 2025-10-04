#include "kernels.h"
#include <map>
#include <string>

// The dispatcher function that returns the correct kernel
matmul_func_t get_kernel_for_mode(const std::string& mode) {
    // Update the map to point to the newly named functions
    static const std::map<std::string, matmul_func_t> kernel_map = {
        {"avx", kernel_avx},
        {"scalar", kernel_scalar},
        {"hybrid", kernel_hybrid},
        {"interleaved", kernel_interleaved}
    };

    auto it = kernel_map.find(mode);
    if (it != kernel_map.end()) {
        return it->second;
    }
    return nullptr; // Return null if mode is not found
}
