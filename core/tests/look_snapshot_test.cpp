// Host-side checks for core/viz/looks/LookSnapshot: the look the shell hands a renderer whole (geode_viz_set_look,
// geode_stage_set_look) and the latch that passes on only the parts that differ.
//
// What is pinned: the first look is every part, and so is the first on a new latch (a renderer made again);
// the same look again is nothing, even as new copies of equal params and tuning; each part is told apart, the
// user's empty routes from the style's own; the latch keeps the look last taken; the defaults a renderer starts
// on are the ones the shell reads; the transition length and the preset morph are held to what the user may set.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "viz/looks/LookSnapshot.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

LookSnapshot lookAt(float value) {
    LookSnapshot l;
    l.sceneId = "tunnel";
    l.params = {value, 0.5f};
    l.lfos = "wave " + std::to_string(value);
    l.adsrs = "envelope a";
    l.tuning = {0.5f};
    l.transitionId = "fade";
    l.transitionMs = 1200;
    return l;
}

void parts() {
    LookLatch latch;
    check(latch.take(lookAt(77.0f)) == kLookAll, "the first look is every part");
    check(latch.take(lookAt(77.0f)) == 0, "the same look again, as new copies, is nothing");
    check(latch.take(lookAt(93.0f)) == (kLookParams | kLookLfos), "only the parts that differ");
    LookSnapshot next = lookAt(93.0f);
    next.sceneId = "bloom";
    next.transitionMs = 400;
    check(latch.take(next) == (kLookScene | kLookTransition), "a new scene and a new transition length");
    next.transitionId = "random";
    check(latch.take(next) == kLookTransition, "a new transition id");
    next.hasReactions = true;
    check(latch.take(next) == kLookReactions, "an empty user text is not the style's own");
    next.reactions = "route a";
    next.reducedMotion = true;
    next.tuning = {0.75f};
    next.adsrs = "envelope b";
    check(latch.take(next) == (kLookReactions | kLookReducedMotion | kLookTuning | kLookAdsrs), "routes, reduced motion, tuning and envelopes");
    next.tuning.clear();
    check(latch.take(next) == kLookTuning, "back to the engine's tuning");
    next.hasReactions = false;
    next.reactions.clear();
    check(latch.take(next) == kLookReactions, "back to the style's own routes");
    check(latch.look().sceneId == "bloom" && latch.look().transitionMs == 400, "and it keeps the look last taken");

    LookLatch remade;
    check(remade.take(latch.look()) == kLookAll, "a renderer made again gets the whole look");
}

void defaults() {
    const LookSnapshot first;
    check(first.sceneId == kFirstSceneId && first.transitionId == kFirstTransitionId && first.transitionMs == kFirstTransitionMs,
          "an unset look is the renderer's first one");
    check(lookDefaultsText() == "neon_chrome_sea\nfade\n1200\n0.6\n0.12\n20\n30\n8\n",
          "the shell reads scene, transition, length, rise, fall, both timers and the morph's top, a line each");
}

void clamps() {
    check(clampTransitionMs(kFirstTransitionMs) == kFirstTransitionMs, "the first length is a length the user may set");
    check(clampTransitionMs(0) == kTransitionMsMin && clampTransitionMs(-5) == kTransitionMsMin, "too short is the shortest");
    check(clampTransitionMs(60000) == kTransitionMsMax, "too long is the longest");
    check(clampTransitionMs(2750) == 2750, "a length in range is kept");
    check(clampPresetMorphSec(2.5f) == 2.5f, "a glide in range is kept");
    check(clampPresetMorphSec(40.0f) == kPresetMorphSecMax, "too long a glide is the longest");
    check(clampPresetMorphSec(0.0f) == 0.0f && clampPresetMorphSec(-1.0f) == 0.0f, "none or less is none");
    check(clampPresetMorphSec(std::nanf("")) == 0.0f, "NaN is none");
}

}  // namespace

int main() {
    parts();
    defaults();
    clamps();
    if (failures) {
        std::fprintf(stderr, "%d look snapshot check(s) failed\n", failures);
        return 1;
    }
    std::printf("look snapshot: all checks passed\n");
    return 0;
}
