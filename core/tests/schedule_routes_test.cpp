// Host-side checks that an export's reaction routes follow its style schedule (#771). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned: when core/viz/StyleSchedule switches style partway through a segment (a take's timed
// switch, or a beat cut), the renderer hands the switch to ReactionResolve::followScene (Renderer's scheduled
// scene hook, geode_viz::followScheduledScene), so the routes it plays are the opening style's DNA before
// the switch and the new style's DNA from the switch frame on, still under the same user routes and hand
// mask: the user's routes on the moments the mask hands them are the same on both sides of the switch.
//
// The frame loop below is geode_viz_render's, minus GL: syncReactions (resolve before the render), then
// beginFrame's applyStyleSchedule (the switch, handed on only when it differs from the requested scene),
// then the reaction matrix reads what plays.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "viz/ReactionResolve.hpp"
#include "viz/StyleSchedule.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr const char* kA = "neon_chrome_sea";
constexpr const char* kB = "glow_vortex";
constexpr int kSeed = 7;

// SceneRegistry stands in, as in reaction_resolve_test.
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

bool sameRoutes(const RouteArray& a, const RouteArray& b) {
    for (int i = 0; i < ReactionEngine::kRoutes; ++i) {
        if (!sameRoute(a[static_cast<size_t>(i)], b[static_cast<size_t>(i)])) return false;
    }
    return true;
}

// What a style should play: its DNA, or the user's routes merged with it under the mask.
RouteArray expected(std::string_view scene, const HandRoutes* user) {
    RouteArray out{};
    const StyleDna dna = buildStyleDna(scene, kSeed, traitsOf(scene));
    HandRoutes h;
    if (user) {
        h = *user;
        h.mask &= hand::kAll;
    }
    mergeHand(h, dna, out);
    return out;
}

// Two routes of the user's: one on Bass, handed to them by the mask (the moment Bass belongs to), one on
// Treble, which the mask leaves to the DNA unless the two share a moment.
HandRoutes userList() {
    const int m = hand::momentOf(ReactSource::Bass);
    HandRoutes h;
    h.user = true;
    h.mask = m >= 0 ? (1u << m) : hand::kUnclaimed;
    h.count = 2;
    ReactRoute& bass = h.routes[0];
    bass.enabled = true;
    bass.source = ReactSource::Bass;
    bass.target = LfoTarget::Zoom;
    bass.mode = ReactMode::Follow;
    bass.amount = 1.3f;
    ReactRoute& hat = h.routes[1];
    hat.enabled = true;
    hat.source = ReactSource::Treble;
    hat.target = LfoTarget::Bloom;
    hat.amount = 0.5f;
    return h;
}

// The user's routes the mask hands them, in order: mergeHand plays exactly these first.
std::vector<ReactRoute> claimed(const HandRoutes& user) {
    std::vector<ReactRoute> out;
    const ReactSourceMask mine = hand::sources(user.mask & hand::kAll);
    for (int i = 0; i < user.count; ++i) {
        const ReactRoute& r = user.routes[static_cast<size_t>(i)];
        if (mine[static_cast<size_t>(r.source)]) out.push_back(r);
    }
    return out;
}

bool leadsWith(const RouteArray& playing, const std::vector<ReactRoute>& head) {
    if (head.size() > playing.size()) return false;
    for (size_t i = 0; i < head.size(); ++i) {
        if (!sameRoute(playing[i], head[i])) return false;
    }
    return true;
}

// One renderer's reaction side over a segment: the resolver geode_viz owns, the scene the renderer has
// requested, the schedule it steps, and what the matrix plays.
struct Segment {
    ReactionResolve resolve{&traitsOf};
    std::string requested;
    StyleSchedule schedule;
    RouteArray playing{};

    Segment(const char* opening, const HandRoutes* user) : requested(opening) {
        resolve.setScene(opening);  // geode_viz_set_scene before the export's first frame
        resolve.setSeed(kSeed);     // geode_viz_set_params
        if (user) resolve.setHand(*user);  // geode_viz_set_user_reactions
    }

    // One frame at `timeMs`, `beatCount` the sound engine's.
    void frame(double timeMs, float beatCount) {
        RouteArray routes{};
        if (resolve.resolve(routes)) playing = routes;  // syncReactions
        if (const StyleCue* cue = schedule.step(timeMs, beatCount)) {  // applyStyleSchedule
            if (cue->sceneId != requested) {
                requested = cue->sceneId;  // Renderer::setScene accepted it
                if (resolve.followScene(cue->sceneId, routes)) playing = routes;  // the scheduled scene hook
            }
        }
    }
};

StyleCue cue(double atMs, const char* id) {
    StyleCue c;
    c.atMs = atMs;
    c.sceneId = id;
    return c;
}

void testTheDnaDiffers() {
    check(!sameRoutes(expected(kA, nullptr), expected(kB, nullptr)), "A's and B's DNA differ, so the test sees the switch");
    const HandRoutes user = userList();
    check(!sameRoutes(expected(kA, &user), expected(kB, &user)), "so do their merges with the user's routes");
    check(!claimed(user).empty(), "the mask hands the user at least one route");
}

// A take that opens on A and switches to B at 2 s; 60 fps.
void runTimedSwitch(const HandRoutes* user, const char* what) {
    Segment s(kA, user);
    s.schedule.set({cue(0.0, kA), cue(2000.0, kB)}, {}, 4);
    const RouteArray wantA = expected(kA, user);
    const RouteArray wantB = expected(kB, user);
    bool beforeOk = true;
    bool afterOk = true;
    bool userKept = true;
    const std::vector<ReactRoute> head = user ? claimed(*user) : std::vector<ReactRoute>{};
    for (int f = 0; f < 240; ++f) {
        const double t = f * 1000.0 / 60.0;
        s.frame(t, static_cast<float>(f / 30));
        if (t < 2000.0) {
            beforeOk = beforeOk && sameRoutes(s.playing, wantA);
        } else {
            afterOk = afterOk && sameRoutes(s.playing, wantB);
        }
        userKept = userKept && leadsWith(s.playing, head);
    }
    std::printf("  %s: %d resolves\n", what, s.resolve.resolves());
    check(beforeOk, "before the timed switch the routes are A's");
    check(afterOk, "from the switch frame on they are B's");
    check(userKept, "the user's routes under the mask are the same across the switch");
    check(s.resolve.resolves() == 2, "one resolve to open, one at the switch, none on the frames between");
}

// A beat cut A, B every 4 beats (a beat every 30 frames at 60 fps): A for 120 frames, then B, then A again.
void testBeatCut() {
    const HandRoutes user = userList();
    Segment s(kA, &user);
    s.schedule.set({}, {kA, kB}, 4);
    const RouteArray wantA = expected(kA, &user);
    const RouteArray wantB = expected(kB, &user);
    bool ok = true;
    for (int f = 0; f < 360; ++f) {
        s.frame(f * 1000.0 / 60.0, static_cast<float>(f / 30));
        const bool onB = f >= 120 && f < 240;
        ok = ok && sameRoutes(s.playing, onB ? wantB : wantA);
    }
    check(ok, "a beat cut moves the routes to B on its beat and back to A on the next cut");
    check(s.resolve.resolves() == 3, "one resolve per cut");
}

// After a scheduled switch the shell's own inputs still apply on top of the new style.
void testUserEditAfterSwitch() {
    Segment s(kA, nullptr);
    s.schedule.set({cue(500.0, kB)}, {}, 4);
    for (int f = 0; f < 60; ++f) s.frame(f * 1000.0 / 60.0, 0.0f);
    check(sameRoutes(s.playing, expected(kB, nullptr)), "with no list of the user's the switch plays B's DNA");
    const HandRoutes user = userList();
    s.resolve.setHand(user);
    s.frame(1000.0, 0.0f);
    check(sameRoutes(s.playing, expected(kB, &user)), "a user edit after the switch merges with B's DNA, not A's");
}

}  // namespace

int main() {
    testTheDnaDiffers();
    runTimedSwitch(nullptr, "DNA alone");
    const HandRoutes user = userList();
    runTimedSwitch(&user, "with the user's routes");
    testBeatCut();
    testUserEditAfterSwitch();
    if (failures) {
        std::printf("schedule_routes_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("schedule_routes_test: ok\n");
    return 0;
}
