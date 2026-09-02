#include "analysis/WindowTable.hpp"

#include <cmath>

namespace geode::analysis {

WindowTable::WindowTable(int size, WindowShape shape) : size_(size), table_(static_cast<size_t>(size)) {
    for (int i = 0; i < size; i++) {
        table_[i] = shape == WindowShape::Rectangular
            ? 1.0f
            : static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * i / size));
    }
}

void WindowTable::applyInto(const float* source, int sourceLength, int sourceOffset, float* out) const {
    for (int i = 0; i < size_; i++) {
        const int at = sourceOffset + i;
        out[i] = (at >= 0 && at < sourceLength) ? source[at] * table_[i] : 0.0f;
    }
}

}  // namespace geode::analysis
