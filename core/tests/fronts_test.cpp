// Fronts (core/viz/fronts/FrontField.hpp): spawn, step and expire, slot reuse when all eight are alive, where a
// front is at 30, 60, 90 and 144 fps, the packed uniform array, and that an idle field asks for no pass.
#include <cmath>
#include <cstdio>
#include <string>

#include "viz/fronts/FrontField.hpp"

using namespace geode::viz::fronts;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

Spawn sweep(float life = 2.0f) {
    Spawn s = Spawn::fromEdge(Edge::Left, Kind::Line);
    s.speed = 0.5f;
    s.life_s = life;
    return s;
}

void spawnStepExpire() {
    FrontField f;
    check(!f.live() && f.count() == 0, "a new field is idle");
    const int slot = f.spawnFront(sweep(1.0f));
    check(slot == 0 && f.alive(slot) && f.live() && f.count() == 1, "a spawn takes the first free slot");
    f.step(0.5f);
    check(f.alive(slot) && near(f.age(slot), 0.5f), "it ages by dt");
    f.step(0.5f);
    check(!f.alive(slot) && !f.live(), "it is gone at the end of its life");
    f.step(1.0f);
    check(!f.live(), "stepping an idle field changes nothing");
    check(f.spawnFront(sweep()) == 0, "a freed slot is taken again");
}

void refusesBadSpawns() {
    FrontField f;
    Spawn s = sweep();
    s.life_s = 0.0f;
    check(f.spawnFront(s) == -1, "no life, no front");
    s = sweep();
    s.speed = NAN;
    check(f.spawnFront(s) == -1, "a non-finite number is refused");
    s = sweep();
    s.dirX = 0.0f;
    s.dirY = 0.0f;
    s.strength = 5.0f;
    s.palettePos = 1.25f;
    check(f.spawnFront(s) == 0, "a zero direction is mended, not refused");
    const Packed p = f.pack();
    check(near(p.data[2], 1.0f) && near(p.data[3], 0.0f), "the direction falls back to a unit vector");
    check(near(p.data[10], 0.25f), "the palette position wraps");
    f.step(0.5f);
    check(f.pack().data[8] <= 1.0f, "strength is clamped to 1");
    f.step(NAN);
    f.step(-1.0f);
    check(near(f.age(0), 0.5f), "a bad or negative dt changes nothing");
}

void reusesTheOldestSlot() {
    FrontField f;
    for (int i = 0; i < kMaxFronts; ++i) {
        check(f.spawnFront(sweep(100.0f)) == i, "slots fill in order");
        f.step(0.1f);
    }
    check(f.count() == kMaxFronts, "eight are alive");
    check(f.spawnFront(sweep(100.0f)) == 0, "the ninth reuses the oldest (slot 0)");
    check(f.count() == kMaxFronts && near(f.age(0), 0.0f), "still eight, the new one starts at age 0");
    f.step(0.1f);
    check(f.spawnFront(sweep(100.0f)) == 1, "then the oldest is slot 1");
    f.step(0.1f);
    f.clear();
    check(!f.live(), "clear empties it");
}

void sameDistanceAtAnyFrameRate() {
    for (float hz : {30.0f, 60.0f, 90.0f, 144.0f}) {
        FrontField f;
        Spawn s = sweep(5.0f);
        s.speed = 0.8f;
        const int slot = f.spawnFront(s);
        const int frames = static_cast<int>(hz);
        for (int i = 0; i < frames; ++i) f.step(1.0f / hz);
        check(near(f.distance(slot), 0.8f, 2e-4f), "one second travels 0.8 frame heights at " + std::to_string(static_cast<int>(hz)) + " fps");
        check(near(f.pack().data[6], 0.8f, 2e-4f), "and packs the same distance at " + std::to_string(static_cast<int>(hz)) + " fps");
    }
}

void packsTheLiveFronts() {
    FrontField f;
    Spawn a = Spawn::fromEdge(Edge::Top, Kind::Ring);
    a.effect = Effect::Invert;
    a.width = 0.1f;
    a.spread = 0.5f;
    a.palettePos = 0.75f;
    Spawn b = sweep(0.2f);
    f.spawnFront(b);
    f.spawnFront(a);
    f.step(0.1f);
    Packed p = f.pack();
    check(p.count == 2, "two live fronts pack");
    const float* second = p.data.data() + kFloatsPerFront;
    check(near(second[0], 0.5f) && near(second[1], 1.0f) && near(second[3], -1.0f), "origin and direction come first");
    check(second[4] == 1.0f && second[5] == 3.0f && near(second[7], 0.1f), "then kind, effect and width");
    check(near(second[9], 0.5f) && near(second[10], 0.75f) && near(p.palettePos[1], 0.75f), "then spread and palette position");
    f.step(0.2f);
    p = f.pack();
    check(p.count == 1, "an expired front drops out of the pack, the rest close up");
    check(near(p.data[10], 0.75f), "the survivor moved to the front of the list");
    check(near(p.data[kFloatsPerFront + 8], 0.0f), "unused entries stay zero");
}

void envelopeFadesInAndOut() {
    check(envelope(0.0f, 2.0f) == 0.0f, "no strength at birth");
    check(near(envelope(1.0f, 2.0f), 1.0f), "full strength in the middle");
    check(envelope(1.9f, 2.0f) < envelope(1.5f, 2.0f), "fading toward the end");
    check(envelope(2.0f, 2.0f) == 0.0f && envelope(-1.0f, 2.0f) == 0.0f && envelope(1.0f, 0.0f) == 0.0f, "none outside its life");
}

void idleAsksForNoPass() {
    FrontField f;
    check(!f.live() && f.pack().count == 0, "an idle field draws nothing");
    f.spawnFront(sweep(0.1f));
    check(f.live(), "a front asks for the pass");
    f.step(0.1f);
    check(!f.live() && f.pack().count == 0, "and stops asking when it ends");
}

// The sign every Reveal rests on: a pixel the front has passed is at a negative distance and shows the other
// picture; one it has not reached is at a positive distance and shows what was ahead (fronts_frag.glsl).
void revealSignIsBehindTheFront() {
    Spawn s = sweep();
    const float aspect = 1.0f;
    const float travelled = 0.5f;
    const float behind = lineDistance(s, travelled, 0.2f, 0.5f, aspect);
    const float ahead = lineDistance(s, travelled, 0.8f, 0.5f, aspect);
    check(behind < 0.0f && ahead > 0.0f, "a pixel behind the front is at a negative distance, one ahead at a positive");
    const float w = 0.05f;
    check(revealPassed(behind, w) > 0.99f, "a Reveal shows the other picture where the front has passed");
    check(revealPassed(ahead, w) < 0.01f, "and what was ahead where it has not reached");
    check(near(revealPassed(0.0f, w), 0.5f), "half way on the line itself");
}

void sweepCrossesAFrameOfAnyAspect() {
    const float aspects[] = {0.46f, 1.0f, 1.78f};
    for (const float a : aspects) {
        const Spawn left = Spawn::fromEdge(Edge::Left, Kind::Line, a);
        check(near(left.speed * left.life_s, a), "a sweep from the left crosses a frame as wide as it is");
        const Spawn bottom = Spawn::fromEdge(Edge::Bottom, Kind::Line, a);
        check(near(bottom.speed * bottom.life_s, 1.0f), "a sweep from the bottom crosses a frame's height");
    }
    const Spawn square = Spawn::fromEdge(Edge::Left, Kind::Line);
    check(near(square.speed, 0.5f), "an unset aspect reads as square");
}

void refusesUnknownKindsEffectsAndCapsLife() {
    FrontField f;
    Spawn s = sweep();
    s.kind = static_cast<Kind>(7);
    check(f.spawnFront(s) == -1, "an unknown kind is refused");
    s = sweep();
    s.effect = static_cast<Effect>(-1);
    check(f.spawnFront(s) == -1, "an unknown effect is refused");
    check(f.spawns() == 0, "a refused spawn is not counted");
    s = sweep(1.0e6f);
    const int slot = f.spawnFront(s);
    f.step(kMaxLifeSeconds + 1.0f);
    check(slot == 0 && !f.alive(slot), "a long life is cut to the longest");
    check(f.spawns() == 1, "a spawn is counted");
}

}  // namespace

int main() {
    spawnStepExpire();
    refusesBadSpawns();
    reusesTheOldestSlot();
    sameDistanceAtAnyFrameRate();
    packsTheLiveFronts();
    envelopeFadesInAndOut();
    idleAsksForNoPass();
    revealSignIsBehindTheFront();
    sweepCrossesAFrameOfAnyAspect();
    refusesUnknownKindsEffectsAndCapsLife();
    if (failures == 0) std::printf("fronts_test: ok\n");
    return failures == 0 ? 0 : 1;
}
