// Host checks for signal::PlayingRoutes (#352): the reaction routes a renderer plays follow the style it has
// asked for now, so an export's StyleSchedule switch re-deals the new style's DNA, and a user list merges
// under the hand mask exactly as geode_style_reactions_hand does. Built and run by CI (.github/workflows/
// android.yml, "Native host tests") with the system compiler; no GL, no Renderer.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "signal/bindings/PlayingRoutes.hpp"
#include "viz/ReactionDna.hpp"
#include "viz/Reactions.hpp"

using namespace geode;
using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// A stand-in for SceneRegistry: one style turns, every one reads the shader look.
StyleTraits traitsOf(const std::string& id) { return {id == "glow_vortex" ? 1.0f : 0.0f, true}; }

bool sameRoute(const ReactRoute& x, const ReactRoute& y) {
    return x.enabled == y.enabled && x.source == y.source && x.target == y.target && x.mode == y.mode && x.amount == y.amount &&
           x.curve == y.curve && x.attackSeconds == y.attackSeconds && x.releaseSeconds == y.releaseSeconds;
}

bool sameRoutes(const RouteArray& a, int an, const RouteArray& b, int bn) {
    if (an != bn) return false;
    for (int i = 0; i < ReactionEngine::kRoutes; ++i) {
        if (!sameRoute(a[static_cast<size_t>(i)], b[static_cast<size_t>(i)])) return false;
    }
    return true;
}

// What geode_style_reactions_hand gives for the same inputs: the reference the renderer must match.
int expected(const std::string& id, int seed, const HandRoutes& user, std::uint32_t mask, RouteArray& out) {
    HandRoutes h = user;
    h.mask = h.user ? (mask & hand::kAll) : 0u;
    return mergeHand(h, buildStyleDna(id, seed, traitsOf(id)), out);
}

bool plays(const signal::PlayingRoutes& r, const std::string& id, int seed, const HandRoutes& user, std::uint32_t mask) {
    RouteArray want;
    const int n = expected(id, seed, user, mask, want);
    return sameRoutes(r.routes(), r.count(), want, n);
}

// The first source the mask hands to the user, for a route of theirs.
ReactSource ownedSource(std::uint32_t mask) {
    const ReactSourceMask mine = hand::sources(mask);
    for (int s = 0; s < kReactSourceCount; ++s) {
        if (mine[static_cast<size_t>(s)]) return static_cast<ReactSource>(s);
    }
    return ReactSource::Bass;
}

void dnaFollowsTheRequestedStyle() {
    const HandRoutes none{};
    signal::PlayingRoutes r;
    check(r.resolve("neon_chrome_sea", 0, 0u, &traitsOf), "the first resolve always hands the resolver routes");
    check(plays(r, "neon_chrome_sea", 0, none, 0u), "no list of the user's: the style's DNA");
    check(!r.resolve("neon_chrome_sea", 0, 0u, &traitsOf), "nothing changed: no new routes");

    // A take's switch (StyleSchedule → setScene): the next latch deals the new style's DNA, not the first one's.
    check(r.resolve("glow_vortex", 0, 0u, &traitsOf), "a style switch re-resolves");
    check(plays(r, "glow_vortex", 0, none, 0u), "after the switch: the new style's DNA");
    RouteArray first;
    const int firstCount = expected("neon_chrome_sea", 0, none, 0u, first);
    const bool differsB = !sameRoutes(r.routes(), r.count(), first, firstCount);
    check(r.resolve("bulb_julia", 0, 0u, &traitsOf), "a beat cut to a third style re-resolves");
    check(plays(r, "bulb_julia", 0, none, 0u), "after the cut: the third style's DNA");
    const bool differsC = !sameRoutes(r.routes(), r.count(), first, firstCount);
    check(differsB || differsC, "a later style does not keep the first style's DNA");

    // A keyframed reactSeed re-deals too.
    check(r.resolve("bulb_julia", 4, 0u, &traitsOf), "a new seed re-resolves");
    check(plays(r, "bulb_julia", 4, none, 0u), "the new seed's DNA");
    // The mask means nothing without a list of the user's.
    check(!r.resolve("bulb_julia", 4, hand::kAll, &traitsOf), "the hand mask alone does not re-deal the DNA");
}

void aUserListMergesUnderTheFramesMask() {
    const std::uint32_t mask = hand::bit(Moment::Kick);
    HandRoutes user;
    user.user = true;
    user.count = 1;
    user.mask = hand::kAll;  // ignored: the frame's mask decides
    ReactRoute& mine = user.routes[0];
    mine.enabled = true;
    mine.source = ownedSource(mask);
    mine.target = LfoTarget::Zoom;
    mine.amount = 0.7f;

    signal::PlayingRoutes r;
    r.resolve("neon_chrome_sea", 0, mask, &traitsOf);
    r.setUser(user);
    check(r.resolve("neon_chrome_sea", 0, mask, &traitsOf), "a new list of the user's re-resolves");
    check(plays(r, "neon_chrome_sea", 0, user, mask), "the hand wins per moment, as geode_style_reactions_hand");
    check(r.count() > 0 && sameRoute(r.routes()[0], mine), "the user's route plays first");

    check(r.resolve("glow_vortex", 0, mask, &traitsOf), "a style switch re-resolves with a list too");
    check(plays(r, "glow_vortex", 0, user, mask), "the user's routes over the new style's DNA");
    check(r.count() > 0 && sameRoute(r.routes()[0], mine), "the user's route survives the switch");

    check(r.resolve("glow_vortex", 0, hand::kAll, &traitsOf), "with a list, a new mask re-resolves");
    check(plays(r, "glow_vortex", 0, user, hand::kAll), "the whole list under kAll");
    check(r.resolve("glow_vortex", 0, 0u, &traitsOf), "an empty mask re-resolves");
    check(plays(r, "glow_vortex", 0, user, 0u), "nothing the hand's: the DNA alone");

    r.setUser(HandRoutes{});
    check(r.resolve("glow_vortex", 0, 0u, &traitsOf), "dropping the list re-resolves");
    check(plays(r, "glow_vortex", 0, HandRoutes{}, 0u), "back to the style's DNA");
}

}  // namespace

int main() {
    dnaFollowsTheRequestedStyle();
    aUserListMergesUnderTheFramesMask();
    if (failures == 0) std::printf("playing_routes_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
