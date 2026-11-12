#pragma once
#include <string_view>
#include <vector>

// A batch render's queue rules (core/output): which tracks a batch renders, and how far along it is. The tracks,
// their titles and each one's outcome are the shell's list; only these rules are here.
namespace geode::output::batch {

// The indices of the tracks a batch renders, in order, each track (by its URI) once: a queue or a hand-made
// list can hold one twice, and rendering it twice would only make two identical files.
std::vector<int> firstOfEach(const std::vector<std::string_view>& uris);

// The whole batch's progress, 0..1: the tracks done plus the share of the one rendering (clamped to 0..1). An
// empty or finished batch is done.
float progress(int done, int total, float withinCurrent);

}  // namespace geode::output::batch
