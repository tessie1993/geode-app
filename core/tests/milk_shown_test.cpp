// Host-side checks for when a loaded .milk preset counts as shown (core/viz/MilkShown). No GL, no projectM.
//
// What is pinned: nothing is reported at load; the path is reported once, after kShownFrames clean frames;
// a frame with an error starts the count over; a newer load replaces the one not yet reported.
#include <cstdio>
#include <string>

#include "viz/MilkShown.hpp"

namespace milk = geode::viz::milk;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Frames until drew() reports, or -1 within the limit.
int framesUntilReport(milk::ShownReport& r, std::string& out, int limit) {
    for (int i = 1; i <= limit; ++i) {
        out = r.drew(true);
        if (!out.empty()) return i;
    }
    return -1;
}

}  // namespace

int main() {
    constexpr int n = milk::ShownReport::kShownFrames;
    std::string out;

    milk::ShownReport idle;
    check(framesUntilReport(idle, out, 3 * n) == -1, "nothing is reported before a load");

    milk::ShownReport r;
    r.loaded("/milk/a.milk");
    check(framesUntilReport(r, out, 3 * n) == n, "a load is reported after kShownFrames frames");
    check(out == "/milk/a.milk", "the report names the loaded path");
    check(framesUntilReport(r, out, 3 * n) == -1, "a load is reported once");

    r.loaded("/milk/b.milk");
    for (int i = 0; i < n - 1; ++i) r.drew(true);
    check(r.drew(false).empty(), "a frame with an error is no report");
    check(framesUntilReport(r, out, 3 * n) == n, "an error starts the count over");

    r.loaded("/milk/c.milk");
    for (int i = 0; i < n / 2; ++i) r.drew(true);
    r.loaded("/milk/d.milk");
    check(framesUntilReport(r, out, 3 * n) == n && out == "/milk/d.milk", "a newer load replaces the unreported one");

    if (failures == 0) std::printf("milk_shown_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
