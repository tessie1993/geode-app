// Host-side checks for the process-wide Customize layer (core/viz/LayerState, #364). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned: one value every renderer reads (live, export, wallpaper, dream), so a layer set once is
// what each of them draws; the generation moves on a real change and only then, so a renderer re-reads it
// exactly when it changed; a renderer made after the set still sees it; and an export's pinned copy keeps
// the layer it started with while the live one moves on.
#include <cstdio>
#include <limits>

#include "viz/LayerState.hpp"

namespace ls = geode::viz::layer_state;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

ls::Layer layer(const char* id, float mix, int blend) {
    ls::Layer l;
    l.sceneId = id;
    l.mix = mix;
    l.blend = blend;
    return l;
}

void setAndGet() {
    ls::reset();
    check(ls::get().sceneId.empty(), "no layer until one is set");
    ls::set(layer("fluid", 0.5f, 2));
    const ls::Layer got = ls::get();
    check(got.sceneId == "fluid" && got.mix == 0.5f && got.blend == 2, "set is what get returns");
    ls::set(layer("fluid", 3.0f, 2));
    check(ls::get().mix == 1.0f, "the mix is clamped to 1");
    ls::set(layer("fluid", -1.0f, 2));
    check(ls::get().mix == 0.0f, "the mix is clamped to 0");
    ls::set(layer("fluid", std::numeric_limits<float>::quiet_NaN(), 2));
    check(ls::get().mix == 0.0f, "a non-finite mix is 0");
    ls::set(layer("", 0.5f, 1));
    check(ls::get().sceneId.empty(), "an empty id is no layer");
}

void generationMovesOnChangeOnly() {
    ls::reset();
    const uint64_t g0 = ls::generation();
    ls::set(layer("silk", 0.4f, 1));
    const uint64_t g1 = ls::generation();
    check(g1 > g0, "a new layer moves the generation");
    ls::set(layer("silk", 0.4f, 1));
    check(ls::generation() == g1, "setting the same layer again does not");
    ls::set(layer("silk", 0.6f, 1));
    const uint64_t g2 = ls::generation();
    check(g2 > g1, "a new mix moves it");
    ls::set(layer("silk", 0.6f, 4));
    const uint64_t g3 = ls::generation();
    check(g3 > g2, "a new blend moves it");
    ls::set(layer("silk", 7.0f, 4));
    ls::set(layer("silk", 9.0f, 4));
    const uint64_t g4 = ls::generation();
    ls::set(layer("silk", 1.0f, 4));
    check(ls::generation() == g4, "two out-of-range mixes that clamp alike are one change");
}

void followerFollows() {
    ls::reset();
    ls::set(layer("life", 0.3f, 1));
    ls::Follower f;  // made after the set, as a wallpaper or dream renderer is
    ls::Layer out;
    check(f.poll(out) && out.sceneId == "life", "a new follower sees the layer set before it");
    check(!f.poll(out), "nothing new, nothing handed over");
    ls::set(layer("acid", 0.3f, 1));
    check(f.poll(out) && out.sceneId == "acid", "a change is handed over");
    ls::set(layer("acid", 0.3f, 1));
    check(!f.poll(out), "a set that changes nothing is not");
    ls::reset();
    check(f.poll(out) && out.sceneId.empty(), "clearing the layer is handed over too");
}

void pinnedCopyIgnoresLaterSets() {
    ls::reset();
    ls::set(layer("cymatics", 0.7f, 3));
    ls::Follower exportView;
    exportView.pin();  // geode_viz_set_offscreen(1), before the export's first frame
    ls::Layer out;
    check(exportView.pinned(), "pinned after pin");
    check(exportView.poll(out) && out.sceneId == "cymatics" && out.mix == 0.7f, "the pin hands over the layer once");
    ls::set(layer("fluid", 0.2f, 1));
    check(!exportView.poll(out), "a live edit during the render never reaches the pinned copy");
    ls::reset();
    check(!exportView.poll(out), "nor does clearing it");
    exportView.unpin();
    check(!exportView.pinned(), "unpinned after unpin");
    check(exportView.poll(out) && out.sceneId.empty(), "unpinned, it follows the store again");

    ls::set(layer("silk", 0.5f, 1));
    ls::Follower live;
    check(live.poll(out) && out.sceneId == "silk", "a live follower beside it moves on");
}

}  // namespace

int main() {
    setAndGet();
    generationMovesOnChangeOnly();
    followerFollows();
    pinnedCopyIgnoresLaterSets();
    if (failures) {
        std::printf("%d layer state check(s) failed\n", failures);
        return 1;
    }
    std::printf("layer_state_test: all checks passed\n");
    return 0;
}
