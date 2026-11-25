// Host test: a new play starts the reaction matrix over (signal::Resolver::newPlay). What the matrix smoothed and
// learned from the last song (a route's output gliding down from a loud passage, the sections and followers behind
// its derived sources) must not carry into the next song's first frames; the routes themselves stay.
// Built by CI with the resolver's sources (see resolver_test in host-tests.txt).
#include <array>
#include <string>
#include <cmath>
#include <cstdio>
#include <memory>

#include "viz/modulation/Reactions.hpp"
#include "viz/resolver/Resolver.hpp"

using geode::signal::EngineSettings;
using geode::signal::Resolver;
using geode::signal::StyleContext;
using geode::viz::LfoTarget;
using geode::viz::ReactionEngine;
using geode::viz::ReactRoute;
using geode::viz::ReactSource;
using geode::viz::SceneParams;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

constexpr float kDt = 1.0f / 60.0f;
const std::string kStyle = "neon_chrome_sea";

// A driven song (the reactions scale by the drive), the bass loud or silent.
GeodeSoundFrame song(float bass) {
    GeodeSoundFrame f{};
    f.drive = 0.8f;
    f.loudness = 0.85f;
    f.level = 0.6f;
    f.vol = f.volAtt = 1.0f;
    f.mid = f.midAtt = 1.0f;
    f.treble = f.trebleAtt = 1.0f;
    f.bass = f.bassAtt = bass;
    return f;
}

std::unique_ptr<Resolver> withRoute() {
    auto r = std::make_unique<Resolver>();
    std::array<ReactRoute, ReactionEngine::kRoutes> routes{};
    routes[0].enabled = true;
    routes[0].source = ReactSource::Bass;
    routes[0].target = LfoTarget::Twist;
    routes[0].amount = 0.6f;
    routes[0].releaseSeconds = 1.0f;
    r->setReactionRoutes(routes);
    return r;
}

float quietTwist(Resolver& r, const EngineSettings& s) {
    const StyleContext st{&kStyle, 0.0f};
    return r.step(SceneParams{}, song(0.0f), kDt, s, st).params.twist;
}

void loudPassage(Resolver& r, const EngineSettings& s) {
    const StyleContext st{&kStyle, 0.0f};
    for (int i = 0; i < 120; ++i) r.step(SceneParams{}, song(2.0f), kDt, s, st);
}

void testNewPlayForgetsTheLastSongsRoutes() {
    const EngineSettings s;
    auto fresh = withRoute();
    const float first = quietTwist(*fresh, s);

    auto stale = withRoute();
    loudPassage(*stale, s);
    const float carried = quietTwist(*stale, s);
    check(std::fabs(carried - first) > 1e-3f, "without a new play the loud passage still glides down (the test can tell)");

    auto restarted = withRoute();
    loudPassage(*restarted, s);
    restarted->newPlay();
    const float after = quietTwist(*restarted, s);
    check(std::fabs(after - first) < 1e-5f, "after a new play the first frame is a fresh matrix's first frame");
}

void testNewPlayKeepsTheRoutes() {
    const EngineSettings s;
    const StyleContext st{&kStyle, 0.0f};
    auto r = withRoute();
    loudPassage(*r, s);
    r->newPlay();
    const float quiet = r->step(SceneParams{}, song(0.0f), kDt, s, st).params.twist;
    r->newPlay();
    const float loud = r->step(SceneParams{}, song(2.0f), kDt, s, st).params.twist;
    check(std::fabs(quiet - loud) > 1e-3f, "the route still hears the bass after a new play");
}

}  // namespace

int main() {
    testNewPlayForgetsTheLastSongsRoutes();
    testNewPlayKeepsTheRoutes();
    if (failures) {
        std::printf("new_play_reactions_test: %d failed\n", failures);
        return 1;
    }
    std::printf("new_play_reactions_test: all passed\n");
    return 0;
}
