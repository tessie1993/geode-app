#include "output/Chapters.hpp"

#include <algorithm>
#include <utility>

namespace geode::output {

std::vector<ChapterSpan> chaptersFromClips(const int64_t* startsMs, const int64_t* durationsMs, int count) {
    std::vector<ChapterSpan> out;
    if (!startsMs || !durationsMs || count < 2) return out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        out.push_back({std::max<int64_t>(0, startsMs[i]), std::max<int64_t>(0, durationsMs[i]), i});
    }
    return out;
}

std::vector<ChapterSpan> chaptersFromMarkers(const int64_t* markersMs, int count, int64_t totalMs) {
    std::vector<ChapterSpan> out;
    if (!markersMs || count < 2) return out;
    std::vector<std::pair<int64_t, int>> marks;
    marks.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) marks.emplace_back(std::max<int64_t>(0, markersMs[i]), i);
    std::stable_sort(marks.begin(), marks.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    out.reserve(marks.size() + 1);
    if (marks.front().first > 0) out.push_back({0, marks.front().first, ChapterSpan::kLeadIn});
    for (size_t i = 0; i < marks.size(); ++i) {
        const int64_t start = marks[i].first;
        const int64_t next = i + 1 < marks.size() ? marks[i + 1].first : totalMs;
        out.push_back({start, std::max(next, start) - start, marks[i].second});
    }
    return out;
}

}  // namespace geode::output
