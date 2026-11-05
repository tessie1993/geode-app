#pragma once
#include <string>
#include <string_view>

#include "viz/ReactionDna.hpp"

namespace geode::viz {

// What one renderer's reaction matrix plays, and when to work it out again (#750).
//
// The routes are a pure function of four inputs: the scene drawn (the one the shell asked for through
// geode_viz_set_scene, or the one an export's StyleSchedule switched to since, #771), the params' reactSeed, the user's routes and
// Customize › Music's hand mask. With no list of the user's that is the style's DNA (buildStyleDna); with one
// it is mergeHand, the hand winning per moment. Building the DNA every frame would be wasted work, so this
// keeps the inputs and resolves only when they differ from the ones it last resolved. The mask means
// nothing without a list of the user's, so the hand is normalised first (no user: mask 0) and a mask change
// alone does not re-resolve the DNA.
//
// Pure: the style's traits come through `traits` (SceneRegistry on the device, a stub on the host), so
// core/tests/reaction_resolve_test can drive it. Not thread-safe; geode_viz guards it.
class ReactionResolve {
public:
    using TraitsOf = StyleTraits (*)(std::string_view styleId);

    explicit ReactionResolve(TraitsOf traits) : traits_(traits) {}

    void setScene(std::string_view id) {
        scene_.assign(id);
        sceneSet_ = true;
    }

    // A switch the renderer made on its own, mid-segment (an export's StyleSchedule, by time or by beat; #771):
    // the new style is the scene from now on, and what plays is worked out at once, under the same seed, user
    // routes and hand mask, so the switch frame already plays the new style's DNA. The same as setScene then
    // resolve; true when `out` was written. Live, every switch (Auto, the director's moments, a swipe) comes
    // in through geode_viz_set_scene instead.
    bool followScene(std::string_view id, RouteArray& out) {
        setScene(id);
        return resolve(out);
    }

    void setSeed(int seed) { seed_ = seed; }

    // The user's side: `hand.user` false is "all Auto", whatever its routes and mask say.
    void setHand(const HandRoutes& hand) { hand_ = normalised(hand); }

    // True when the inputs differ from the ones last resolved (or nothing was resolved yet) and a scene is
    // set: `out` then holds what plays, slots past the count disabled. False leaves `out` alone. Inputs that
    // change and change back between two calls resolve nothing.
    bool resolve(RouteArray& out) {
        if (!sceneSet_) return false;
        if (resolved_ && scene_ == lastScene_ && seed_ == lastSeed_ && same(hand_, lastHand_)) return false;
        resolved_ = true;
        lastScene_ = scene_;
        lastSeed_ = seed_;
        lastHand_ = hand_;
        ++resolves_;
        const StyleDna dna = buildStyleDna(scene_, seed_, traits_ ? traits_(scene_) : StyleTraits{});
        mergeHand(hand_, dna, out);
        return true;
    }

    // How many times the routes were worked out (the test's count).
    int resolves() const { return resolves_; }

private:
    static HandRoutes normalised(const HandRoutes& h) {
        HandRoutes n;
        if (!h.user) return n;
        n.user = true;
        n.count = h.count < 0 ? 0 : (h.count > ReactionEngine::kRoutes ? ReactionEngine::kRoutes : h.count);
        for (int i = 0; i < n.count; ++i) n.routes[static_cast<size_t>(i)] = h.routes[static_cast<size_t>(i)];
        n.mask = h.mask & hand::kAll;
        return n;
    }

    static bool same(const ReactRoute& a, const ReactRoute& b) {
        return a.enabled == b.enabled && a.source == b.source && a.target == b.target && a.mode == b.mode &&
               a.amount == b.amount && a.curve == b.curve && a.attackSeconds == b.attackSeconds &&
               a.releaseSeconds == b.releaseSeconds;
    }

    static bool same(const HandRoutes& a, const HandRoutes& b) {
        if (a.user != b.user || a.count != b.count || a.mask != b.mask) return false;
        for (int i = 0; i < a.count; ++i) {
            if (!same(a.routes[static_cast<size_t>(i)], b.routes[static_cast<size_t>(i)])) return false;
        }
        return true;
    }

    TraitsOf traits_;
    // What the shell set last.
    std::string scene_;
    bool sceneSet_ = false;
    int seed_ = 0;
    HandRoutes hand_{};
    // What the routes were last worked out from.
    bool resolved_ = false;
    std::string lastScene_;
    int lastSeed_ = 0;
    HandRoutes lastHand_{};
    int resolves_ = 0;
};

}  // namespace geode::viz
