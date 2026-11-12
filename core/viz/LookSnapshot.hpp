#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The look snapshot (GLOSSARY): everything the user set that decides what a renderer draws, as the shell hands it
// whole (geode_viz_set_look). The latch keeps the one set last, so only the parts that differ reach the renderer.
// Pure: host-tested in core/tests/look_snapshot_test.cpp.
namespace geode::viz {

// The look a renderer starts on before one is set: the app's first style (FreeTier.FIRST_STYLE_ID) and a fade.
inline constexpr char kFirstSceneId[] = "neon_chrome_sea";
inline constexpr char kFirstTransitionId[] = "fade";
inline constexpr int64_t kFirstTransitionMs = 1200;

// Those defaults for the shell (geode_viz_look_defaults): scene id, transition id, transition ms, each line ended.
inline std::string lookDefaultsText() {
    return std::string(kFirstSceneId) + '\n' + kFirstTransitionId + '\n' + std::to_string(kFirstTransitionMs) + '\n';
}

// The waves, the envelopes and the routes are their saved texts; no tuning is the engine's defaults.
struct LookSnapshot {
    std::string sceneId = kFirstSceneId;
    std::vector<float> params;
    std::string lfos;
    std::string adsrs;
    std::vector<float> tuning;
    bool hasReactions = false;
    std::string reactions;
    bool reducedMotion = false;
    std::string transitionId = kFirstTransitionId;
    int64_t transitionMs = kFirstTransitionMs;
};

enum LookPart : uint32_t {
    kLookScene = 1u << 0,
    kLookParams = 1u << 1,
    kLookLfos = 1u << 2,
    kLookAdsrs = 1u << 3,
    kLookTuning = 1u << 4,
    kLookReactions = 1u << 5,
    kLookReducedMotion = 1u << 6,
    kLookTransition = 1u << 7,
    kLookAll = (1u << 8) - 1,
};

// Which parts of a look differ from the one before, by value, so only those reach the renderer: a scene asked for
// again would restart nothing, but a transition or a morph set again would. The first look is all of them, so a
// new renderer (a new latch) gets the whole look.
class LookLatch {
public:
    uint32_t take(LookSnapshot next) {
        uint32_t changed = kLookAll;
        if (any_) {
            changed = 0;
            if (next.sceneId != look_.sceneId) changed |= kLookScene;
            if (next.params != look_.params) changed |= kLookParams;
            if (next.lfos != look_.lfos) changed |= kLookLfos;
            if (next.adsrs != look_.adsrs) changed |= kLookAdsrs;
            if (next.tuning != look_.tuning) changed |= kLookTuning;
            if (next.hasReactions != look_.hasReactions || next.reactions != look_.reactions) changed |= kLookReactions;
            if (next.reducedMotion != look_.reducedMotion) changed |= kLookReducedMotion;
            if (next.transitionId != look_.transitionId || next.transitionMs != look_.transitionMs) changed |= kLookTransition;
        }
        any_ = true;
        look_ = std::move(next);
        return changed;
    }
    const LookSnapshot& look() const { return look_; }

private:
    LookSnapshot look_;
    bool any_ = false;
};

}  // namespace geode::viz
