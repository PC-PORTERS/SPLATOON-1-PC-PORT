#pragma once
#include <fstream>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace gx2vk {
inline std::vector<char> read_cache_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto size = file.tellg();
    if (size <= 0 || uint64_t(size) > std::numeric_limits<size_t>::max() ||
        uint64_t(size) > uint64_t(std::numeric_limits<std::streamsize>::max()))
        return {};
    std::vector<char> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}
} // namespace gx2vk
