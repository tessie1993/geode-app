// Things (core/viz/things/ThingPool.hpp): spawn, expire and refusals, slot reuse, where a Thing is at 30, 60, 90
// and 144 fps, edge crossing, one Collide event per contact, the bounce, chain depth and its per-step cap, bounded
// gravity, morph, melt, packing, and that an idle pool asks for no pass.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "viz/things/ThingPool.hpp"

using namespace geode::viz::things;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

Spawn drifter(float x, float y, float vx, float vy, float life = 5.0f) {
    Spawn s;
    s.x = x;
    s.y = y;
    s.vx = vx;
    s.vy = vy;
    s.size = 0.02f;
    s.life_s = life;
    return s;
}

void stepFor(ThingPool& p, float seconds, int fps) {
    const int n = static_cast<int>(seconds * static_cast<float>(fps) + 0.5f);
    for (int i = 0; i < n; ++i) p.step(1.0f / static_cast<float>(fps));
}

void spawnStepExpire() {
    ThingPool p;
    check(!p.live() && p.count() == 0, "a new pool is idle");
    Spawn s = drifter(0.2f, 0.3f, 0.1f, 0.0f, 0.5f);
    const int slot = p.spawn(s);
    check(slot >= 0 && p.alive(slot) && p.live(), "a spawn takes a slot");
    p.step(0.25f);
    check(p.alive(slot) && near(p.age(slot), 0.25f), "it ages with dt");
    check(near(p.view(slot).x, 0.2f + 0.1f * 0.25f, 0.002f), "and moves by its velocity");
    p.step(0.3f);
    check(!p.alive(slot) && !p.live(), "it expires at the end of its life");
}

void refusesBadInput() {
    ThingPool p;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Spawn s = drifter(0.5f, 0.5f, 0.0f, 0.0f);
    Spawn a = s;
    a.x = nan;
    Spawn b = s;
    b.life_s = 0.0f;
    Spawn c = s;
    c.size = 0.0f;
    Spawn d = s;
    d.shape = static_cast<Shape>(9);
    Spawn e = s;
    e.vy = std::numeric_limits<float>::infinity();
    check(p.spawn(a) == -1 && p.spawn(b) == -1 && p.spawn(c) == -1 && p.spawn(d) == -1 && p.spawn(e) == -1,
          "a non-finite number, no life, no size or an unknown shape is refused");
    check(!p.live(), "and nothing is alive after");
    check(p.burst(nan, 0.5f, 4, 0.2f) == 0 && p.burst(0.5f, 0.5f, 0, 0.2f) == 0 && !p.live(), "a bad burst makes nothing");
    const int slot = p.spawn(s);
    p.step(nan);
    p.step(-1.0f);
    p.step(0.0f);
    check(near(p.age(slot), 0.0f), "a non-finite or non-positive dt changes nothing");
    check(!p.morph(slot, Shape::Ring, nan) && !p.morph(slot, Shape::Ring, -1.0f) && !p.morph(slot, static_cast<Shape>(7), 1.0f),
          "a bad morph is refused");
    check(!p.melt(slot, 0.0f) && !p.melt(slot, nan) && !p.melt(31, 1.0f), "a bad melt is refused");
}

void reusesTheOldestSlot() {
    ThingPool p;
    for (int i = 0; i < kMaxThings; ++i) {
        check(p.spawn(drifter(0.5f, 0.5f, 0.0f, 0.0f, 10.0f)) == i, "slots fill in order");
        p.step(0.01f);
    }
    check(p.count() == kMaxThings, "all slots taken");
    check(p.spawn(drifter(0.1f, 0.1f, 0.0f, 0.0f, 10.0f)) == 0, "the oldest slot is reused");
    check(p.count() == kMaxThings && near(p.age(0), 0.0f), "and it starts afresh");
    check(p.spawn(drifter(0.1f, 0.1f, 0.0f, 0.0f, 10.0f)) == 1, "then the next oldest");
}

void sameplaceAtAnyFrameRate() {
    float xs[4];
    float ys[4];
    float ox[4];
    float oy[4];
    const int rates[4] = {30, 60, 90, 144};
    for (int k = 0; k < 4; ++k) {
        ThingPool p;
        const int a = p.spawn(drifter(0.1f, 0.2f, 0.3f, -0.1f, 3.0f));
        Spawn o = drifter(0.5f, 0.5f, 0.2f, 0.0f, 3.0f);
        o.behaviour = Drift | Orbit;
        o.orbit = 2.0f;
        const int b = p.spawn(o);
        stepFor(p, 1.0f, rates[k]);
        xs[k] = p.view(a).x;
        ys[k] = p.view(a).y;
        ox[k] = p.view(b).x;
        oy[k] = p.view(b).y;
        check(near(p.age(a), 1.0f, 0.01f), "one second passed");
    }
    for (int k = 0; k < 4; ++k) {
        check(near(xs[k], 0.4f, 0.003f) && near(ys[k], 0.1f, 0.003f), "a drifting Thing is where speed x time puts it");
        check(near(xs[k], xs[0], 0.003f) && near(ys[k], ys[0], 0.003f), "the same at every frame rate");
        check(near(ox[k], ox[0], 0.003f) && near(oy[k], oy[0], 0.003f), "an orbiting Thing too");
    }
}

void crossesFromEdgeToEdge() {
    const Edge edges[4] = {Edge::Left, Edge::Right, Edge::Bottom, Edge::Top};
    for (Edge e : edges) {
        ThingPool p;
        const int slot = p.spawn(Spawn::fromEdge(e, Shape::Ring, 0.05f, 2.0f));
        check(slot >= 0, "a Thing from an edge spawns");
        const View v0 = p.view(slot);
        const bool horizontal = e == Edge::Left || e == Edge::Right;
        const float start = horizontal ? v0.x : v0.y;
        check(start < -0.05f || start > 1.05f, "it begins off the picture");
        stepFor(p, 1.0f, 60);
        const View mid = p.view(slot);
        check(near(horizontal ? mid.x : mid.y, 0.5f, 0.01f), "it is in the middle half way");
        stepFor(p, 0.9f, 60);
        const float late = horizontal ? p.view(slot).x : p.view(slot).y;
        check(p.alive(slot) && (start < 0.0f ? late > 1.0f : late < 0.0f), "and past the other edge before it ends");
        stepFor(p, 0.2f, 60);
        check(!p.alive(slot), "then it is gone");
    }
}

void collisionFiresOncePerContact() {
    ThingPool p;
    p.spawn(drifter(0.3f, 0.5f, 0.5f, 0.0f));
    p.spawn(drifter(0.7f, 0.5f, -0.5f, 0.0f));
    int frames = 0;
    int overlapFrames = 0;
    for (int i = 0; i < 90; ++i) {
        p.step(1.0f / 60.0f);
        ++frames;
        const View a = p.view(0);
        const View b = p.view(1);
        if (std::fabs(b.x - a.x) < a.size + b.size) ++overlapFrames;
    }
    check(overlapFrames > 3, "they overlap for several frames");
    const EventList ev = p.events();
    check(ev.count == 1, "one event for the whole contact, not one a frame");
    check(ev.items[0].type == EventType::Collide && ev.items[0].a == 0 && ev.items[0].b == 1, "it names the pair");
    check(near(ev.items[0].x, 0.5f, 0.03f) && near(ev.items[0].y, 0.5f, 0.001f), "and where they met");
    const EventList drained = p.drainEvents();
    check(drained.count == 1 && p.events().count == 0, "drain returns them and empties the list");
    check(frames == 90, "ran the frames");
}

void bounceKeepsThemApart() {
    ThingPool p;
    Spawn a = drifter(0.3f, 0.5f, 0.5f, 0.0f);
    a.behaviour = Drift | Bounce;
    Spawn b = drifter(0.7f, 0.5f, -0.5f, 0.0f);
    b.behaviour = Drift | Bounce;
    p.spawn(a);
    p.spawn(b);
    float closest = 10.0f;
    for (int i = 0; i < 48; ++i) {
        p.step(1.0f / 60.0f);
        closest = std::fmin(closest, std::fabs(p.view(1).x - p.view(0).x));
    }
    check(closest >= 0.04f - 1e-3f, "two bouncing Things never sink into each other");
    check(p.view(0).vx < 0.0f && p.view(1).vx > 0.0f, "they turn back");
    check(near(std::fabs(p.view(0).vx), 0.5f, 0.02f), "an elastic bounce keeps the speed of equal Things");
    check(p.events().count >= 1, "the touch is still an event");
    // The edge: a bouncing Thing turns back and stays on the picture.
    ThingPool q;
    Spawn w = drifter(0.9f, 0.5f, 0.6f, 0.0f, 10.0f);
    w.behaviour = Drift | Bounce;
    q.spawn(w);
    for (int i = 0; i < 300; ++i) {
        q.step(1.0f / 60.0f);
        check(q.view(0).x <= 1.0f && q.view(0).x >= 0.0f, "it stays inside the edges");
    }
}

void chainDepthAndCap() {
    ThingPool p;
    Spawn s = drifter(0.5f, 0.5f, 0.0f, 0.0f, 0.95f);
    s.chain = 2;
    s.chainCount = 2;
    const int root = p.spawn(s);
    for (int i = 0; i < 9; ++i) p.step(0.1f);
    check(p.alive(root) && p.count() == 1, "the parent is alive until its end");
    p.step(0.1f);
    check(p.count() == 2, "it leaves its children where it died");
    int seen = 0;
    for (int i = 0; i < kMaxThings; ++i) {
        if (!p.alive(i)) continue;
        ++seen;
        check(p.view(i).chain == 1 && p.view(i).size < 0.02f, "each is one deeper and smaller");
    }
    check(seen == 2, "two children");
    for (int i = 0; i < 8; ++i) p.step(0.1f);
    check(p.count() == 4, "and theirs after them");
    for (int i = 0; i < 40; ++i) p.step(0.1f);
    check(!p.live(), "depth 0 leaves none, so it ends");

    ThingPool q;
    Spawn big = drifter(0.5f, 0.5f, 0.0f, 0.0f, 0.5f);
    big.chain = 1;
    big.chainCount = 6;
    big.size = 0.01f;
    for (int i = 0; i < 10; ++i) {
        big.x = 0.05f + 0.09f * static_cast<float>(i);
        q.spawn(big);
    }
    q.step(0.6f);
    check(q.count() == kMaxChainPerStep, "one step's deaths spawn no more than the cap, whatever the parents");
}

void gravityIsBounded() {
    ThingPool p;
    Spawn g = drifter(0.5f, 0.5f, 0.0f, 0.0f, 20.0f);
    g.behaviour = Gravity;
    g.pull = 2.0f;
    g.size = 0.01f;
    const int src = p.spawn(g);
    Spawn t = drifter(0.8f, 0.5f, 0.0f, 0.0f, 20.0f);
    t.size = 0.01f;
    const int near1 = p.spawn(t);
    Spawn far = t;
    far.x = 0.95f;
    far.y = 0.95f;
    p.spawn(far);
    p.step(0.2f);
    check(p.view(near1).x < 0.8f, "a Thing is pulled toward the one with Gravity");
    check(near(p.view(src).x, 0.5f, 1e-6f) && near(p.view(src).y, 0.5f, 1e-6f), "which itself stays put");
    float prevVx = p.view(near1).vx;
    float prevVy = p.view(near1).vy;
    for (int i = 0; i < 600; ++i) {
        p.step(1.0f / 60.0f);
        const View v = p.view(near1);
        check(std::isfinite(v.x) && std::isfinite(v.y), "positions stay finite");
        check(std::hypot(v.vx, v.vy) <= kMaxSpeed + 1e-3f, "speed stays under the cap");
        check(std::hypot(v.vx - prevVx, v.vy - prevVy) <= kMaxAccel / 60.0f + 0.01f, "and the pull in a frame stays under the clamp");
        prevVx = v.vx;
        prevVy = v.vy;
    }
}

void morphProgresses() {
    ThingPool p;
    const int slot = p.spawn(drifter(0.5f, 0.5f, 0.0f, 0.0f, 5.0f));
    check(near(p.morphProgress(slot), 1.0f), "no morph running reads as settled");
    check(p.morph(slot, Shape::Blob, 1.0f), "a morph starts");
    check(near(p.morphProgress(slot), 0.0f), "from nothing");
    stepFor(p, 0.5f, 60);
    check(near(p.morphProgress(slot), 0.5f, 0.02f), "half way after half the time");
    float out[2];
    const Packed mid = p.pack();
    out[0] = mid.data[4];
    out[1] = mid.data[5];
    check(out[0] == static_cast<float>(Shape::Dot) && out[1] == static_cast<float>(Shape::Blob), "the pack names both shapes");
    stepFor(p, 0.6f, 60);
    check(near(p.morphProgress(slot), 1.0f) && p.view(slot).shape == Shape::Blob, "it arrives and stays");
    check(p.morph(slot, Shape::Ring, 0.0f) && near(p.morphProgress(slot), 1.0f) && p.view(slot).shape == Shape::Ring,
          "zero seconds is at once");
}

void meltFinishesAtExpiry() {
    ThingPool p;
    const int slot = p.spawn(drifter(0.5f, 0.5f, 0.0f, 0.0f, 5.0f));
    check(near(p.meltProgress(slot), 0.0f), "not melting");
    check(p.melt(slot, 1.0f), "a melt starts");
    check(near(p.remaining(slot), 1.0f), "it now ends a second from now, not at the old life");
    stepFor(p, 0.5f, 60);
    check(near(p.meltProgress(slot), 0.5f, 0.02f), "half melted half way");
    check(p.melt(slot, 3.0f) && near(p.remaining(slot), 0.5f, 0.02f), "a later melt never lengthens it");
    stepFor(p, 0.45f, 60);
    check(p.alive(slot) && p.meltProgress(slot) > 0.9f, "nearly done just before the end");
    const Packed pk = p.pack();
    check(pk.count == 1 && pk.data[7] > 0.9f, "the pass is told");
    stepFor(p, 0.1f, 60);
    check(!p.alive(slot), "gone exactly when the melt is done");
}

void packsAndCompacts() {
    ThingPool p;
    p.spawn(drifter(0.1f, 0.5f, 0.0f, 0.0f, 5.0f));
    Spawn shortLived = drifter(0.2f, 0.5f, 0.0f, 0.0f, 0.2f);
    p.spawn(shortLived);
    Spawn third = drifter(0.3f, 0.5f, 0.0f, 0.0f, 5.0f);
    third.palettePos = 1.25f;
    p.spawn(third);
    p.step(0.5f);
    const Packed pk = p.pack();
    check(pk.count == 2, "only the live Things are packed");
    check(near(pk.data[0], 0.1f) && near(pk.data[kFloatsPerThing], 0.3f), "in slot order, the gap closed");
    check(near(pk.palettePos[1], 0.25f), "a palette position is wrapped to 0..1");
    check(pk.data[8] > 0.0f && pk.data[kFloatsPerThing + 8] > 0.0f, "each has an amount");
    check(near(pk.data[2], 0.02f), "and its size");
    check(p.pack().count == 2 && Packed{}.count == 0, "packing changes nothing");
}

void idleAsksForNoPass() {
    ThingPool p;
    check(!p.live() && p.pack().count == 0, "an idle pool draws nothing");
    Spawn s = drifter(0.5f, 0.5f, 0.0f, 0.0f, 0.1f);
    p.spawn(s);
    check(p.live(), "a Thing asks for the pass");
    p.step(0.2f);
    check(!p.live() && p.pack().count == 0, "and stops asking when it ends");
    p.spawn(s);
    p.clear();
    check(!p.live() && p.events().count == 0, "clear empties the pool");
}

void burstMakesARing() {
    ThingPool p;
    Spawn base = drifter(0.0f, 0.0f, 0.0f, 0.0f, 3.0f);
    check(p.burst(0.5f, 0.5f, 6, 0.3f, base) == 6 && p.count() == 6, "a burst makes n Things");
    float sx = 0.0f;
    float sy = 0.0f;
    for (int i = 0; i < 6; ++i) {
        sx += p.view(i).vx;
        sy += p.view(i).vy;
        check(near(std::hypot(p.view(i).vx, p.view(i).vy), 0.3f, 1e-3f), "each moves out at the speed");
    }
    check(near(sx, 0.0f) && near(sy, 0.0f), "evenly round the ring");
    p.step(0.5f);
    check(p.events().count == 0, "a burst's own Things are not born touching");
}

}  // namespace

int main() {
    spawnStepExpire();
    refusesBadInput();
    reusesTheOldestSlot();
    sameplaceAtAnyFrameRate();
    crossesFromEdgeToEdge();
    collisionFiresOncePerContact();
    bounceKeepsThemApart();
    chainDepthAndCap();
    gravityIsBounded();
    morphProgresses();
    meltFinishesAtExpiry();
    packsAndCompacts();
    idleAsksForNoPass();
    burstMakesARing();
    if (failures == 0) std::printf("things_test: ok\n");
    return failures == 0 ? 0 : 1;
}
