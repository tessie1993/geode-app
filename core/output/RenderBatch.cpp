#include "output/RenderBatch.hpp"

#include <algorithm>
#include <unordered_set>

namespace geode::output::batch {

std::vector<int> firstOfEach(const std::vector<std::string_view>& uris) {
    std::vector<int> keep;
    std::unordered_set<std::string_view> seen;
    keep.reserve(uris.size());
    for (size_t i = 0; i < uris.size(); ++i) {
        if (seen.insert(uris[i]).second) keep.push_back(static_cast<int>(i));
    }
    return keep;
}

float progress(int done, int total, float withinCurrent) {
    if (total <= 0 || done >= total) return 1.0f;
    return (static_cast<float>(done) + std::clamp(withinCurrent, 0.0f, 1.0f)) / static_cast<float>(total);
}

}  // namespace geode::output::batch
