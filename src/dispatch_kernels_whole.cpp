#include "kernels_whole.h"
#include <map>
#include <string>

matmul_whole_func_t get_kernel_for_mode_whole(const std::string& mode) {
    static const std::map<std::string, matmul_whole_func_t> kernel_map = {
        {"scalar_whole", kernel_scalar_whole},
        {"blas_whole", kernel_blas_whole}
    };

    auto it = kernel_map.find(mode);
    if (it != kernel_map.end()) {
        return it->second;
    }
    return nullptr;
}
