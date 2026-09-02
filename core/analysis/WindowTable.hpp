#pragma once
#include <vector>

namespace geode::analysis {

enum class WindowShape { Rectangular, Hann };

class WindowTable {
public:
    explicit WindowTable(int size, WindowShape shape = WindowShape::Hann);
    int size() const { return size_; }
    float coefficient(int index) const { return table_[index]; }
    void applyInto(const float* source, int sourceLength, int sourceOffset, float* out) const;

private:
    int size_;
    std::vector<float> table_;
};

}  // namespace geode::analysis
