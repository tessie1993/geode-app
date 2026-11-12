#pragma once
#include <algorithm>
#include <cstring>
#include <string>

// The C ABI's way of handing a text out: the caller's buffer gets as much as fits, NUL-terminated, and the
// return is the length the whole text needs, so a caller that came up short asks again with room for it.
namespace geode::api {

inline int textOut(const std::string& text, char* out, int capacity) {
    const int need = static_cast<int>(text.size());
    if (out && capacity > 0) {
        const int n = std::min(need, capacity - 1);
        std::memcpy(out, text.data(), static_cast<size_t>(n));
        out[n] = '\0';
    }
    return need;
}

}  // namespace geode::api
