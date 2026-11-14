// Host-side checks for the process-wide Customize layer (core/viz/LayerState, #364). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned: one value every renderer reads (live, export, wallpaper, dream), so a layer set once is
// what each of them draws; the generation moves on a real change and only then, so a renderer re-reads it
// exactly when it changed; a renderer made after the set still sees it; and an export's pinned copy keeps
// the layer it started with while the live one moves on. And the text the shell saves it as: it round trips,
// any other text is the default layer (off), an edit changes one field (of the stored layer too, moving the
// generation only on a real change), and nothing out of range or not finite gets through.
#include <cstdio>
#include <limits>
#include <string>

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

void theTextRoundTrips() {
    check(ls::decode("") == ls::Layer{} && !ls::decode("").on, "no text is the default layer, off");
    check(ls::encode(ls::Layer{}) == "layer|1|0|USER||0|1|0.5", "the default layer's text");
    ls::Layer l;
    l.on = true;
    l.source = ls::Source::Macro;
    l.sceneId = "kifs";
    l.knob = 3;
    l.blend = 5;
    l.mix = 0.3f;
    check(ls::decode(ls::encode(l)) == l, "a layer round trips");
    check(ls::decode(ls::encode(l) + "\n") == l, "a trailing line break is read too");
    for (const char* bad : {"layer|2|1|USER|kifs|0|1|0.5", "layer|1|1|NOPE|kifs|0|1|0.5", "layer|1|2|USER|kifs|0|1|0.5",
                            "layer|1|1|USER|kifs|0|1|x", "layer|1|1|USER|kifs|0|1", "palettestack|1"}) {
        check(ls::decode(bad) == ls::Layer{}, (std::string("unreadable text is the default layer: ") + bad).c_str());
    }
    const ls::Layer wild = ls::decode("layer|1|1|DIRECTOR|a|b\nc|-4|99|7");
    check(wild == ls::Layer{}, "a field split by a separator is unreadable");
    const ls::Layer held = ls::decode("layer|1|1|DIRECTOR|kifs|-4|99|7");
    check(held.on && held.source == ls::Source::Director && held.knob == 0 && held.blend == ls::kDefaultBlend && held.mix == 1.0f,
          "a negative knob is 0, a blend past the list Screen, the mix held to 1");
}

void anEditChangesOneField() {
    using F = ls::Field;
    const ls::Layer base{};
    check(ls::edit(base, F::On, 1.0f).on && !ls::edit(ls::edit(base, F::On, 1.0f), F::On, 0.0f).on, "on, and off");
    check(ls::edit(base, F::Source, 1.0f).source == ls::Source::Director, "source");
    check(ls::edit(base, F::Source, 3.0f) == base && ls::edit(base, F::Source, -1.0f) == base, "a source past the list changes nothing");
    check(ls::edit(base, F::Style, 0.0f, "kifs").sceneId == "kifs" &&
              ls::edit(ls::edit(base, F::Style, 0.0f, "kifs"), F::Style, 0.0f, "").sceneId.empty(),
          "style, and back to native's pick");
    check(ls::edit(base, F::Style, 0.0f, "a|b").sceneId == "ab", "an id loses a separator");
    check(ls::edit(base, F::Knob, 4.0f).knob == 4 && ls::edit(base, F::Knob, -4.0f).knob == 0, "knob, never negative");
    check(ls::edit(base, F::Blend, 3.0f).blend == 3 && ls::edit(base, F::Blend, 8.0f).blend == ls::kDefaultBlend, "blend");
    check(ls::edit(base, F::Mix, 0.25f).mix == 0.25f && ls::edit(base, F::Mix, 2.0f).mix == 1.0f, "mix, held to 0..1");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const F field : {F::On, F::Source, F::Knob, F::Blend, F::Mix}) {
        check(ls::edit(base, field, nan) == base && ls::edit(base, field, inf) == base, "a value that is not finite changes nothing");
    }
    check(ls::edit(base, F::Knob, 3.0e9f).knob == 1000000, "a wild knob is held, not overflowed");
    const ls::Layer on = ls::edit(ls::edit(base, F::On, 1.0f), F::Mix, 0.75f);
    check(on.on && on.mix == 0.75f && on.blend == base.blend && on.source == base.source, "the rest is kept");
}

void aChangeEditsTheStoredLayer() {
    ls::reset();
    const uint64_t g0 = ls::generation();
    check(ls::change(ls::Field::On, 1.0f) && ls::get().on, "a change edits the layer every renderer draws");
    const uint64_t g1 = ls::generation();
    check(g1 > g0, "and moves the generation");
    check(!ls::change(ls::Field::On, 1.0f) && ls::generation() == g1, "the same value again is no change");
    check(ls::change(ls::Field::Style, 0.0f, "kifs") && ls::get().sceneId == "kifs" && ls::get().on, "the rest is kept");
    check(ls::decode(ls::encode(ls::get())) == ls::get(), "what the shell saves reads back as the layer");
    ls::reset();
}

void theShownStyleIsShared() {
    ls::publishShown("kifs");
    check(ls::shown() == "kifs", "what a renderer drew is what the page reads");
    ls::publishShown("");
    check(ls::shown().empty(), "and none");
}

}  // namespace

int main() {
    setAndGet();
    generationMovesOnChangeOnly();
    followerFollows();
    pinnedCopyIgnoresLaterSets();
    theTextRoundTrips();
    anEditChangesOneField();
    aChangeEditsTheStoredLayer();
    theShownStyleIsShared();
    if (failures) {
        std::printf("%d layer state check(s) failed\n", failures);
        return 1;
    }
    std::printf("layer_state_test: all checks passed\n");
    return 0;
}
