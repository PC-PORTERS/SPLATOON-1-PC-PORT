#pragma once
#include <array>
#include <cstring>
#include <string_view>

namespace gx2vk {
// Render-owner front cache only. The full cache retains every entry. Values
// must have stable addresses, and reset() must precede their destruction.
template <size_t Capacity, class T> class LastLookup {
public:
    T* find(std::string_view key) const {
        return value_ && size_ == key.size() && !std::memcmp(bytes_.data(), key.data(), size_) ? value_ : nullptr;
    }
    void remember(std::string_view key, T* value) {
        if (key.size() > Capacity) {
            reset();
            return;
        }
        std::memcpy(bytes_.data(), key.data(), key.size());
        size_ = key.size();
        value_ = value;
    }
    void reset() {
        value_ = nullptr;
        size_ = 0;
    }

private:
    std::array<char, Capacity> bytes_{};
    size_t size_ = 0;
    T* value_ = nullptr;
};
} // namespace gx2vk
