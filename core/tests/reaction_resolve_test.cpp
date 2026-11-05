// Host-side checks for a renderer's reaction resolve (core/viz/ReactionResolve, #750). Built and run by CI
// ("Native host tests" in .github/workflows/native-host-tests.yml) with the system compiler; no GL, no device.
//
// What is pinned: what the native renderer plays equals the old Kotlin path (StyleReactionsCache): the
// style's DNA (geode_style_reactions) with no list of the user's, else the user's routes merged with the DNA
// under the hand mask (geode_style_reactions_hand, mergeHand). And it works the routes out again exactly when
// the seed, the scene, the user's routes or the mask change: never on a repeat of the same inputs, never on
// a mask change while the user has no list (the mask means nothing then), and once for several changes
// made between two renders.
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "viz/ReactionResolve.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// SceneRegistry stands in: the DNA only needs whether a style turns and whether it reads the shader look.
StyleTraits traitsOf(std::string_view id) {
    StyleTraits t;
    t.spinRate = id == "glow_vortex" ? 0.4f : 0.0f;
    t.shaderLook = id.substr(0, 4) == "neon" || id == "glow_vortex";
    return t;
}

bool sameRoute(const ReactRoute& a, const ReactRoute& b) {
    return a.enabled == b.enabled && a.source == b.source && a.target == b.target && a.mode == b.mode &&
           a.amount == b.amount && a.curve == b.curve && a.attackSeconds == b.attackSeconds &&
           a.releaseSeconds == b.releaseSeconds;
}

// The old Kotlin path. No user list: the DNA, padded with disabled routes (ModConfigCodec.packReactions).
// A list: geode_style_reactions_hand, the hand's routes on its sources and the DNA's on the rest.
int oldPath(std::string_view scene, int seed, const HandRoutes* user, RouteArray& out) {
    out = RouteArray{};
    const StyleDna dna = buildStyleDna(scene, seed, traitsOf(scene));
    if (!user) {
        for (int i = 0; i < dna.count; ++i) out[static_cast<size_t>(i)] = dna.routes[static_cast<size_t>(i)];
        return dna.count;
    }
    HandRoutes h = *user;
    h.mask &= hand::kAll;
    return mergeHand(h, dna, out);
}

// Every enabled slot the same, every slot past the count off: what the matrix plays is identical.
bool plays(const RouteArray& got, const RouteArray& want, int count) {
    for (int i = 0; i < ReactionEngine::kRoutes; ++i) {
        const ReactRoute& g = got[static_cast<size_t>(i)];
        const ReactRoute& w = want[static_cast<size_t>(i)];
        if (i < count) {
            if (!sameRoute(g, w)) return false;
        } else if (g.enabled) {
            return false;
        }
    }
    return true;
}

HandRoutes userList(float amount, std::uint32_t mask) {
    HandRoutes h;
    h.user = true;
    h.mask = mask;
    h.count = 2;
    ReactRoute& kick = h.routes[0];
    kick.enabled = true;
    kick.source = ReactSource::Bass;
    kick.target = LfoTarget::Zoom;
    kick.mode = ReactMode::Follow;
    kick.amount = amount;
    ReactRoute& hat = h.routes[1];
    hat.enabled = true;
    hat.source = ReactSource::Treble;
    hat.target = LfoTarget::Bloom;
    hat.amount = 0.5f;
    return h;
}

// One resolve call: did it work the routes out, and is the result the old path's?
bool step(ReactionResolve& r, std::string_view scene, int seed, const HandRoutes* user, const char* what) {
    RouteArray got{};
    const bool resolved = r.resolve(got);
    if (resolved) {
        RouteArray want{};
        const int n = oldPath(scene, seed, user, want);
        check(plays(got, want, n), what);
    }
    return resolved;
}

void testMatchesOldPath() {
    const char* scenes[] = {"neon_chrome_sea", "glow_vortex", "milkdrop", "arbor_1"};
    const std::uint32_t masks[] = {hand::kAll, hand::bit(Moment::Kick), hand::bit(Moment::Hat) | hand::kUnclaimed, 0u};
    for (const char* scene : scenes) {
        for (int seed : {0, 1, 7, 4242}) {
            ReactionResolve r(&traitsOf);
            r.setScene(scene);
            r.setSeed(seed);
            check(step(r, scene, seed, nullptr, "the DNA alone equals geode_style_reactions"), "resolved the DNA");
            for (std::uint32_t mask : masks) {
                const HandRoutes user = userList(1.2f, mask);
                r.setHand(user);
                check(step(r, scene, seed, &user, "the merge equals geode_style_reactions_hand"), "resolved the merge");
            }
        }
    }
}

void testResolvesExactlyOnChange() {
    ReactionResolve r(&traitsOf);
    RouteArray out{};
    check(!r.resolve(out), "nothing to resolve before a scene is set");
    r.setSeed(3);
    check(!r.resolve(out) && r.resolves() == 0, "a seed alone does not resolve without a scene");

    r.setScene("neon_chrome_sea");
    check(step(r, "neon_chrome_sea", 3, nullptr, "first scene"), "the first scene resolves");
    check(!r.resolve(out), "the same inputs do not resolve again");
    r.setScene("neon_chrome_sea");
    r.setSeed(3);
    check(!r.resolve(out) && r.resolves() == 1, "setting the same scene and seed again does not resolve");

    r.setSeed(4);
    check(step(r, "neon_chrome_sea", 4, nullptr, "seed"), "a new seed resolves");
    r.setScene("glow_vortex");
    check(step(r, "glow_vortex", 4, nullptr, "scene"), "a new scene resolves");
    check(r.resolves() == 3, "one resolve per change");

    HandRoutes none;
    none.mask = hand::bit(Moment::Kick);
    r.setHand(none);
    check(!r.resolve(out), "a mask without a list of the user's does not resolve");

    HandRoutes user = userList(1.0f, hand::bit(Moment::Kick));
    r.setHand(user);
    check(step(r, "glow_vortex", 4, &user, "routes"), "the user's routes resolve");
    const HandRoutes copy = userList(1.0f, hand::bit(Moment::Kick));
    r.setHand(copy);
    check(!r.resolve(out), "the same routes again (a new list, equal values) do not resolve");

    user = userList(1.0f, hand::bit(Moment::Kick) | hand::bit(Moment::Snare));
    r.setHand(user);
    check(step(r, "glow_vortex", 4, &user, "mask"), "a new mask resolves");
    HandRoutes outside = user;
    outside.mask |= 1u << 31;  // outside hand::kAll: the same hand once normalised
    r.setHand(outside);
    check(!r.resolve(out), "mask bits outside hand::kAll do not resolve");

    user = userList(-0.7f, user.mask);
    r.setHand(user);
    check(step(r, "glow_vortex", 4, &user, "route values"), "a changed route resolves");

    r.setHand(HandRoutes{});
    check(step(r, "glow_vortex", 4, nullptr, "back to Auto"), "dropping the user's list resolves the DNA");

    const int before = r.resolves();
    r.setScene("arbor_1");
    r.setSeed(9);
    user = userList(0.9f, hand::kAll);
    r.setHand(user);
    check(step(r, "arbor_1", 9, &user, "coalesced"), "several changes before a render resolve");
    check(r.resolves() == before + 1, "several changes between two renders resolve once");

    r.setScene("glow_vortex");
    r.setSeed(10);
    r.setScene("arbor_1");
    r.setSeed(9);
    check(!r.resolve(out) && r.resolves() == before + 1, "inputs changed and changed back between two renders do not resolve");
}

}  // namespace

int main() {
    testMatchesOldPath();
    testResolvesExactlyOnChange();
    if (failures) {
        std::printf("reaction_resolve_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("reaction_resolve_test: ok\n");
    return 0;
}
