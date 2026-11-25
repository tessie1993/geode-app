// Fields (core/viz/fields/FieldSet.hpp): spawn, expire and refusals, slot reuse, force direction for Attract, Repel
// and Swirl, no force past the radius, a clamped finite force at the centre, aspect, the envelope, a Thing pulled
// to the same place at 30, 60, 90 and 144 fps, and that an idle set asks for no pass.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "viz/fields/FieldSet.hpp"
#include "viz/things/ThingPool.hpp"

using namespace geode::viz::fields;
namespace th = geode::viz::things;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

Spawn field(Kind kind, float strength = 1.0f, float life = 5.0f) {
    Spawn s;
    s.kind = kind;
    s.x = 0.5f;
    s.y = 0.5f;
    s.strength = strength;
    s.radius = 0.4f;
    s.life_s = life;
    s.attack_s = 0.0f;
    s.release_s = 0.0f;
    return s;
}

void spawnStepExpire() {
    FieldSet f;
    check(!f.live() && f.count() == 0, "a new set is idle");
    const int slot = f.spawn(field(Kind::Attract, 1.0f, 1.0f));
    check(slot == 0 && f.alive(slot) && f.live() && f.count() == 1, "a spawn takes the first free slot");
    f.step(0.5f);
    check(f.alive(slot) && near(f.age(slot), 0.5f), "it ages by dt");
    f.step(0.5f);
    check(!f.alive(slot) && !f.live(), "it is gone at the end of its life");
    f.step(1.0f);
    check(!f.live(), "stepping an idle set changes nothing");
    check(f.spawn(field(Kind::Swirl)) == 0, "a freed slot is taken again");
}

void refusesBadInput() {
    FieldSet f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Spawn a = field(Kind::Attract);
    a.x = nan;
    Spawn b = field(Kind::Attract);
    b.life_s = 0.0f;
    Spawn c = field(Kind::Attract);
    c.radius = 0.0f;
    Spawn d = field(Kind::Attract);
    d.kind = static_cast<Kind>(7);
    Spawn e = field(Kind::Attract);
    e.vy = inf;
    Spawn g = field(Kind::Attract);
    g.strength = nan;
    check(f.spawn(a) == -1 && f.spawn(b) == -1 && f.spawn(c) == -1 && f.spawn(d) == -1 && f.spawn(e) == -1 && f.spawn(g) == -1,
          "a non-finite number, no life, no radius or an unknown kind is refused");
    check(!f.live(), "and nothing is alive after");
    const int slot = f.spawn(field(Kind::Attract));
    f.step(nan);
    f.step(-1.0f);
    f.step(0.0f);
    check(near(f.age(slot), 0.0f), "a non-finite or non-positive dt changes nothing");
    const Force bad = f.forceAt(nan, 0.5f, 1.0f);
    check(bad.x == 0.0f && bad.y == 0.0f, "a non-finite probe gets no force");
    const Force noAspect = f.forceAt(0.7f, 0.5f, 0.0f);
    check(noAspect.x == 0.0f && noAspect.y == 0.0f, "an unusable aspect gets no force");
}

void reusesTheOldestSlot() {
    FieldSet f;
    for (int i = 0; i < kMaxFields; ++i) {
        check(f.spawn(field(Kind::Attract, 1.0f, 100.0f)) == i, "slots fill in order");
        f.step(0.1f);
    }
    check(f.count() == kMaxFields, "eight are alive");
    check(f.spawn(field(Kind::Repel, 1.0f, 100.0f)) == 0, "the ninth reuses the oldest (slot 0)");
    check(f.count() == kMaxFields && near(f.age(0), 0.0f) && f.view(0).kind == Kind::Repel, "still eight, the new one starts at age 0");
    f.step(0.1f);
    check(f.spawn(field(Kind::Attract, 1.0f, 100.0f)) == 1, "then the oldest is slot 1");
}

void forceDirections() {
    FieldSet at;
    at.spawn(field(Kind::Attract));
    Force e = at.forceAt(0.7f, 0.5f, 1.0f);
    check(e.x < 0.0f && near(e.y, 0.0f), "Attract pulls a point to the east toward the centre");
    Force n = at.forceAt(0.5f, 0.7f, 1.0f);
    check(n.y < 0.0f && near(n.x, 0.0f), "and one to the north");

    FieldSet re;
    re.spawn(field(Kind::Repel));
    e = re.forceAt(0.7f, 0.5f, 1.0f);
    check(e.x > 0.0f && near(e.y, 0.0f), "Repel pushes a point to the east away");

    FieldSet sw;
    sw.spawn(field(Kind::Swirl, 1.0f));
    e = sw.forceAt(0.7f, 0.5f, 1.0f);
    check(near(e.x, 0.0f) && e.y > 0.0f, "a positive Swirl turns counter-clockwise: north of a point to the east");
    n = sw.forceAt(0.5f, 0.7f, 1.0f);
    check(n.x < 0.0f && near(n.y, 0.0f), "and west of a point to the north");

    FieldSet cw;
    cw.spawn(field(Kind::Swirl, -1.0f));
    e = cw.forceAt(0.7f, 0.5f, 1.0f);
    check(near(e.x, 0.0f) && e.y < 0.0f, "a negative Swirl turns clockwise");

    FieldSet neg;
    neg.spawn(field(Kind::Attract, -1.0f));
    check(neg.view(0).strength == 0.0f, "Attract takes no negative strength");
}

void fallsOffAtTheRadius() {
    FieldSet f;
    f.spawn(field(Kind::Attract, 1.0f));
    const float inside = std::fabs(f.forceAt(0.5f + 0.2f, 0.5f, 1.0f).x);
    const float near_edge = std::fabs(f.forceAt(0.5f + 0.36f, 0.5f, 1.0f).x);
    const Force at = f.forceAt(0.5f + 0.41f, 0.5f, 1.0f);
    const Force past = f.forceAt(0.5f + 0.9f, 0.5f, 1.0f);
    check(inside > near_edge && near_edge > 0.0f, "the pull weakens toward the radius");
    check(at.x == 0.0f && at.y == 0.0f && past.x == 0.0f && past.y == 0.0f, "none at or past the radius");
}

void clampedAtTheCentre() {
    FieldSet f;
    for (int i = 0; i < kMaxFields; ++i) f.spawn(field(Kind::Attract, kMaxStrength));
    const float probes[5][2] = {{0.5f, 0.5f}, {0.5f + 1e-7f, 0.5f}, {0.5f, 0.5f - 1e-5f}, {0.51f, 0.5f}, {0.55f, 0.52f}};
    for (const auto& p : probes) {
        const Force v = f.forceAt(p[0], p[1], 1.0f);
        check(std::isfinite(v.x) && std::isfinite(v.y), "the force is finite at the centre");
        check(std::hypot(v.x, v.y) <= kMaxForce + 1e-4f, "and never above the clamp");
    }
    check(std::hypot(f.forceAt(0.55f, 0.5f, 1.0f).x, 0.0f) > kMaxForce - 1e-3f, "eight stacked Fields reach the clamp");
    FieldSet s;
    s.spawn(field(Kind::Swirl, 1.0f));
    const Force c = s.forceAt(0.5f, 0.5f, 1.0f);
    check(std::isfinite(c.x) && std::isfinite(c.y), "a Swirl is finite at its centre");
}

void aspectIsRound() {
    FieldSet f;
    f.spawn(field(Kind::Attract));
    // One distance in frame heights: 0.2 across at aspect 1 is 0.1 of the width at aspect 2.
    const Force a = f.forceAt(0.5f + 0.2f, 0.5f, 1.0f);
    const Force b = f.forceAt(0.5f + 0.1f, 0.5f, 2.0f);
    check(near(b.x, a.x / 2.0f, 1e-4f), "the same distance in frame heights gives the same pull, in uv halved at aspect 2");
    const Force up1 = f.forceAt(0.5f, 0.7f, 1.0f);
    const Force up2 = f.forceAt(0.5f, 0.7f, 2.0f);
    check(near(up1.y, up2.y) && near(up2.x, 0.0f), "a vertical offset does not depend on the aspect");
    // Past the radius in frame heights even though the uv offset is small.
    const Force beyond = f.forceAt(0.5f + 0.25f, 0.5f, 2.0f);
    check(beyond.x == 0.0f, "the radius is round: 0.25 of the width is 0.5 frame heights at aspect 2, past 0.4");
}

void envelopeShapesTheStrength() {
    FieldSet f;
    Spawn s = field(Kind::Swirl, 1.0f, 2.0f);
    s.attack_s = 0.5f;
    s.release_s = 0.5f;
    const int slot = f.spawn(s);
    check(near(f.view(slot).strength, 0.0f), "it starts at nothing");
    f.step(0.25f);
    check(near(f.view(slot).strength, 0.5f, 1e-3f), "half way up the attack it has half its strength");
    f.step(0.75f);
    check(near(f.view(slot).strength, 1.0f, 1e-3f), "it holds its strength between");
    f.step(0.75f);
    check(near(f.view(slot).strength, 0.5f, 1e-3f), "half way down the release it has half again");
    f.step(0.24f);
    check(f.alive(slot) && f.view(slot).strength < 0.01f, "it is nearly gone just before its end");
    f.step(0.1f);
    check(!f.alive(slot), "and gone at the end");
}

void driftsWithTime() {
    float xs[4];
    const int rates[4] = {30, 60, 90, 144};
    for (int k = 0; k < 4; ++k) {
        FieldSet f;
        Spawn s = field(Kind::Attract);
        s.vx = 0.1f;
        const int slot = f.spawn(s);
        const int n = rates[k];
        for (int i = 0; i < n; ++i) f.step(1.0f / static_cast<float>(n));
        xs[k] = f.view(slot).x;
        check(near(xs[k], 0.6f, 1e-3f), "a drifting Field is where speed x time puts it");
    }
    check(near(xs[0], xs[3], 1e-4f), "the same at every frame rate");
}

float pulledX(int fps, float* y) {
    FieldSet f;
    f.spawn(field(Kind::Attract, kMaxStrength, 5.0f));
    th::Spawn t;
    t.x = 0.2f;
    t.y = 0.5f;
    t.size = 0.02f;
    t.life_s = 3.0f;
    th::ThingPool p;
    const int slot = p.spawn(t);
    for (int i = 0; i < fps; ++i) {
        const float dt = 1.0f / static_cast<float>(fps);
        p.step(dt, &f);
        f.step(dt);
    }
    *y = p.view(slot).y;
    return p.view(slot).x;
}

void pullsAThingTheSameAtAnyFrameRate() {
    float y[4];
    float x[4];
    const int rates[4] = {30, 60, 90, 144};
    for (int k = 0; k < 4; ++k) x[k] = pulledX(rates[k], &y[k]);
    check(x[0] > 0.2f + 0.03f, "a Thing near an Attract Field is drawn toward it");
    for (int k = 1; k < 4; ++k) {
        check(near(x[k], x[0], 0.01f) && near(y[k], y[0], 0.01f), "after a second it is in the same place at every frame rate");
    }

    th::ThingPool alone;
    th::Spawn t;
    t.x = 0.2f;
    t.y = 0.5f;
    t.size = 0.02f;
    t.life_s = 3.0f;
    const int slot = alone.spawn(t);
    for (int i = 0; i < 60; ++i) alone.step(1.0f / 60.0f, nullptr);
    check(near(alone.view(slot).x, 0.2f), "with no Fields a still Thing stays where it is");

    th::Spawn still = t;
    still.behaviour = 0u;
    FieldSet f;
    f.spawn(field(Kind::Attract, kMaxStrength, 5.0f));
    th::ThingPool fixed;
    const int fs = fixed.spawn(still);
    for (int i = 0; i < 60; ++i) fixed.step(1.0f / 60.0f, &f);
    check(near(fixed.view(fs).x, 0.2f), "a Thing without Drift is not pulled");
}

void packsAndCompacts() {
    FieldSet f;
    f.spawn(field(Kind::Attract, 1.0f, 1.0f));
    Spawn b = field(Kind::Swirl, -1.0f, 5.0f);
    b.x = 0.25f;
    b.y = 0.75f;
    b.radius = 0.3f;
    f.spawn(b);
    f.step(1.5f);
    const Packed p = f.pack();
    check(p.count == 1, "the packed list holds only the live Fields");
    check(near(p.data[0], 0.25f) && near(p.data[1], 0.75f) && near(p.data[2], 0.3f) && near(p.data[3], 2.0f), "place, radius and kind");
    check(near(p.data[4], -1.0f), "the signed amount");
}

void idleAsksForNoPass() {
    FieldSet f;
    check(!f.live() && f.pack().count == 0, "an idle set has nothing to draw");
    f.spawn(field(Kind::Repel, 1.0f, 0.5f));
    check(f.live() && f.pack().count == 1, "a live one has");
    f.step(0.6f);
    check(!f.live() && f.pack().count == 0, "and an expired one has not");
    const Force none = f.forceAt(0.6f, 0.5f, 1.0f);
    check(none.x == 0.0f && none.y == 0.0f, "an idle set pushes nothing");
}

void lifeIsCappedAndCallsAreCounted() {
    FieldSet f;
    check(f.revision() == 0, "a new set has had no calls");
    const int slot = f.spawn(field(Kind::Swirl, 1.0f, 1.0e6f));
    f.step(kMaxLifeSeconds + 1.0f);
    check(slot == 0 && !f.alive(slot), "a long life is cut to the longest");
    check(f.revision() == 1, "a spawn is counted");
    f.spawn(field(Kind::Attract, NAN));
    check(f.revision() == 1, "a refused spawn is not");
    f.clear();
    check(f.revision() == 2, "a clear is");
}

void thingsAndFieldsShareOneEnvelope() {
    check(envelope(0.3, 2.0, 0.5f, 0.5f) == th::envelope(0.3, 2.0, 0.5f, 0.5f), "one fade for both");
    check(kSoftening == th::kSoftening, "and one softening");
}

}  // namespace

int main() {
    spawnStepExpire();
    refusesBadInput();
    reusesTheOldestSlot();
    forceDirections();
    fallsOffAtTheRadius();
    clampedAtTheCentre();
    aspectIsRound();
    envelopeShapesTheStrength();
    driftsWithTime();
    pullsAThingTheSameAtAnyFrameRate();
    packsAndCompacts();
    idleAsksForNoPass();
    lifeIsCappedAndCallsAreCounted();
    thingsAndFieldsShareOneEnvelope();
    if (failures == 0) std::printf("fields_test: ok\n");
    return failures == 0 ? 0 : 1;
}
