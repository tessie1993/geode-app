#pragma once
#include <cstdint>
#include <string>

#include "viz/ReactionDna.hpp"

namespace geode::signal {

// The reaction routes a renderer plays for the style it has asked for (GLOSSARY "Style DNA").
//
// With no list of the user's this is the style's DNA (viz::buildStyleDna from the style id and the params'
// reactSeed); with one it is "the hand wins per moment" (viz::mergeHand): the user's routes on the moments
// the hand mask gives them, the live DNA on every other. The same builder and merge geode_style_reactions
// and geode_style_reactions_hand run, so the Customize pages show exactly what plays.
//
// WHY the renderer resolves its own (#352, ADR 0003): the shell used to resolve the routes per frame and
// push them, keyed on the style it thought was on. An export's StyleSchedule switches the native scene
// mid-segment without the shell knowing, so every later style in a take or a beat cut played the first
// style's DNA. Resolved here, at the renderer's frame latch, the routes follow whatever style is requested
// now, from the shell or from the schedule, in the same frame.
//
// Re-resolves only when the style, the seed, the user's list or (with a list) the hand mask changes, and
// says so, so the resolver takes new routes only then. Single-threaded: the renderer calls it under its
// frame latch. Allocation-free while nothing changes.
class PlayingRoutes {
public:
    // SceneRegistry's traits for a style id (spin rate, shader look), the DNA's only outside input. A plain
    // function so the host test can stand one in without the registry.
    using TraitsOf = viz::StyleTraits (*)(const std::string& styleId);

    // The user's own routes; `user.user` false is "all Auto" (no list of theirs, the DNA alone). The list's
    // `mask` is ignored: resolve() takes the frame's hand mask, so an export's pinned mask covers it.
    void setUser(const viz::HandRoutes& user);

    // Resolves for `styleId` at `seed` under `handMask` (the frame's latched EngineSettings::handMask).
    // True when routes() changed since the last call (always on the first).
    bool resolve(const std::string& styleId, int seed, std::uint32_t handMask, TraitsOf traitsOf);

    // What plays: count() routes, the slots past it disabled.
    const viz::RouteArray& routes() const { return routes_; }
    int count() const { return count_; }

private:
    viz::HandRoutes user_{};
    bool userDirty_ = true;
    bool resolved_ = false;
    std::string styleId_;
    int seed_ = 0;
    std::uint32_t mask_ = 0;
    viz::RouteArray routes_{};
    int count_ = 0;
};

}  // namespace geode::signal
