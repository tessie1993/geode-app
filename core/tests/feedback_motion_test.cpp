// Host-side checks for the feedback pass's motion (core/viz/liquid/FeedbackMotion), MilkDrop's per-frame
// UV transform made per second. Built and run by CI (.github/workflows/native-host-tests.yml); no GL.
//
// What is pinned: the neutral motion is the identity; zoom (in and out), zoomexp, stretch, rotation about
// an off-centre point and translation each move a sample by MilkDrop's formula; the warp is bounded, boils
// with time, never drifts and scales by its warp scale; the same second of motion lands in the same place
// at 30, 60 and 120 fps; silence is the identity, the bass zooms the MilkDrop way; nothing non-finite gets
// through; the mirror fold is the identity inside 0..1 and continuous at the edges. And the renderer's glue:
// Dive speed and Fly dive in and out, Trails keep their echo zoom and warp, the bass pushes only a dive,
// silence stops it all at 30, 60 and 120 fps, and Liquid's dive lands where its old back-trace did.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "viz/liquid/FeedbackMotion.hpp"

using namespace geode::viz::liquid::motion;
namespace liquid = geode::viz::liquid;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
bool near(Uv a, Uv b, float tol) { return near(a.u, b.u, tol) && near(a.v, b.v, tol); }
bool finite(Uv p) { return std::isfinite(p.u) && std::isfinite(p.v); }

constexpr float kAspects[] = {1.0f, 9.0f / 16.0f, 16.0f / 9.0f};

GeodeSoundFrame playing(float bassAtt, float drive = 0.7f) {
    GeodeSoundFrame s{};
    s.presence = 1.0f;
    s.drive = drive;
    s.bassAtt = bassAtt;
    s.midAtt = 1.0f;
    s.trebleAtt = 1.0f;
    return s;
}

void neutralIsIdentity() {
    const FrameMotion f = perFrame(Motion{}, 1.0f / 60.0f);
    for (float a : kAspects) {
        for (float u = 0.0f; u <= 1.0f; u += 0.125f) {
            for (float v = 0.0f; v <= 1.0f; v += 0.125f) {
                check(near(uvFor(f, {u, v}, a, 3.7f), Uv{u, v}, 1e-5f), "the neutral motion samples where it draws");
                check(near(uvFor(FrameMotion{}, {u, v}, a, 0.0f), Uv{u, v}, 1e-5f), "the neutral frame is the identity");
            }
        }
    }
}

void zoomMovesTowardAndAwayFromTheCentre() {
    const float dt = 1.0f / 60.0f;
    for (float a : kAspects) {
        for (float zoom : {4.0f, 0.5f}) {
            Motion m;
            m.zoom = zoom;
            const float factor = std::pow(zoom, dt);
            for (Uv p : {Uv{0.8f, 0.5f}, Uv{0.3f, 0.2f}, Uv{0.9f, 0.95f}}) {
                const Uv src = uvFor(perFrame(m, dt), p, a, 0.0f);
                const Uv want{0.5f + (p.u - 0.5f) / factor, 0.5f + (p.v - 0.5f) / factor};
                check(near(src, want, 1e-5f), "zoom scales the offset from the centre by 1 / zoom^dt");
            }
        }
    }
    Motion in;
    in.zoom = 4.0f;
    Motion out;
    out.zoom = 0.5f;
    const Uv p{0.8f, 0.7f};
    const Uv srcIn = uvFor(perFrame(in, dt), p, 1.0f, 0.0f);
    const Uv srcOut = uvFor(perFrame(out, dt), p, 1.0f, 0.0f);
    check(std::fabs(srcIn.u - 0.5f) < std::fabs(p.u - 0.5f), "zooming in samples nearer the centre: the picture streams out");
    check(std::fabs(srcOut.u - 0.5f) > std::fabs(p.u - 0.5f), "zooming out samples farther out: the picture streams in");
}

void zoomExpShapesTheZoomByRadius() {
    const float dt = 1.0f / 30.0f;
    Motion m;
    m.zoom = 4.0f;
    m.zoomExp = 2.0f;
    const FrameMotion f = perFrame(m, dt);
    float lastShrink = 0.0f;
    for (float r : {0.25f, 0.5f, 1.0f}) {
        const Uv src = uvFor(f, {0.5f + r * 0.5f, 0.5f}, 1.0f, 0.0f);
        const float zoom2 = std::pow(std::pow(4.0f, dt), std::pow(2.0f, r * 2.0f - 1.0f));
        check(near(src.u, 0.5f + r * 0.5f / zoom2, 1e-5f), "zoomexp: zoom^(zoomexp^(2 rad - 1)), MilkDrop's profile");
        check(near(src.v, 0.5f, 1e-6f), "a point on the axis stays on it");
        const float shrink = r / ((src.u - 0.5f) * 2.0f);
        check(shrink > lastShrink, "zoomexp above 1 zooms the edge faster than the middle");
        lastShrink = shrink;
    }
    const Uv mid = uvFor(f, {0.75f, 0.5f}, 1.0f, 0.0f);
    check(near(mid.u, 0.5f + 0.25f / std::pow(4.0f, dt), 1e-5f), "at radius one half zoomexp leaves the plain zoom");
}

void rotationAboutAnOffCentrePoint() {
    const float dt = 0.1f;
    Motion m;
    m.rot = 1.0f;
    m.cx = 0.3f;
    m.cy = 0.6f;  // MilkDrop's v runs down: this is GL's v 0.4
    const FrameMotion f = perFrame(m, dt);
    const Uv c{0.3f, 0.4f};
    check(near(uvFor(f, c, 1.0f, 0.0f), c, 2e-6f), "the centre of rotation stays put");
    for (Uv p : {Uv{0.7f, 0.4f}, Uv{0.3f, 0.9f}, Uv{0.1f, 0.1f}}) {
        const Uv src = uvFor(f, p, 1.0f, 0.0f);
        const float r0 = std::hypot(p.u - c.u, p.v - c.v);
        const float r1 = std::hypot(src.u - c.u, src.v - c.v);
        check(near(r0, r1, 1e-5f), "rotation keeps the distance to its centre");
        const float cross = (p.u - c.u) * (src.v - c.v) - (p.v - c.v) * (src.u - c.u);
        const float dot = (p.u - c.u) * (src.u - c.u) + (p.v - c.v) * (src.v - c.v);
        check(near(std::fabs(std::atan2(cross, dot)), 0.1f, 1e-4f), "rotation turns by rot * dt");
    }
}

void translationAndStretch() {
    Motion shift;
    shift.dx = 0.2f;
    shift.dy = 0.2f;
    const Uv p{0.4f, 0.6f};
    const Uv s = uvFor(perFrame(shift, 0.05f), p, 1.0f, 0.0f);
    check(near(s.u, 0.39f, 1e-5f), "dx samples from the left: the picture moves right");
    check(near(s.v, 0.61f, 1e-5f), "dy samples from above: the picture moves down");

    Motion stretch;
    stretch.sx = 2.0f;
    stretch.sy = 0.5f;
    stretch.cy = 0.25f;
    const Uv t = uvFor(perFrame(stretch, kMaxDt), p, 1.0f, 0.0f);
    const float kx = std::pow(2.0f, kMaxDt);
    const float ky = std::pow(0.5f, kMaxDt);
    check(near(t.u, 0.5f + (p.u - 0.5f) / kx, 1e-5f), "sx stretches about cx");
    const float vDown = 1.0f - p.v;
    check(near(1.0f - t.v, 0.25f + (vDown - 0.25f) / ky, 1e-5f), "sy stretches about cy, MilkDrop's v down");
    check(perFrame(stretch, 0.5f).sx == perFrame(stretch, kMaxDt).sx, "a long frame is clamped, never a jump");
}

void warpIsBoundedAndBoils() {
    const float dt = 1.0f / 60.0f;
    Motion m;
    m.warp = 2.0f;
    const FrameMotion f = perFrame(m, dt);
    const float amp = 2.0f * kWarpPerSecond * dt;
    check(near(f.warp, amp, 1e-7f), "warp per frame is MilkDrop's 0.0035 at 60 Hz");
    bool moved = false;
    bool boils = false;
    for (float u = 0.0f; u <= 1.0f; u += 0.1f) {
        for (float v = 0.0f; v <= 1.0f; v += 0.1f) {
            const Uv a = uvFor(f, {u, v}, 1.0f, 0.0f);
            const Uv b = uvFor(f, {u, v}, 1.0f, 1.7f);
            check(std::fabs(a.u - u) <= 2.0f * amp + 1e-6f && std::fabs(a.v - v) <= 2.0f * amp + 1e-6f,
                  "two terms per axis bound the warp at twice its amplitude");
            moved = moved || std::fabs(a.u - u) > amp * 0.1f;
            boils = boils || !near(a, b, amp * 0.1f);
        }
    }
    check(moved, "warp moves the samples");
    check(boils, "warp drifts with time");

    for (Uv p : {Uv{0.2f, 0.3f}, Uv{0.5f, 0.5f}, Uv{0.8f, 0.7f}}) {
        double su = 0.0;
        double sv = 0.0;
        const int n = 6000;
        for (int i = 0; i < n; ++i) {
            const Uv src = uvFor(f, p, 1.0f, static_cast<float>(i) * 0.1f);
            su += src.u - p.u;
            sv += src.v - p.v;
        }
        check(std::fabs(su / n) < 0.15 * amp && std::fabs(sv / n) < 0.15 * amp, "warp boils in place, it never drifts");
    }

    Motion still = m;
    still.warpSpeed = 0.0f;
    Motion broad = still;
    broad.warpScale = 2.0f;
    for (Uv p : {Uv{0.2f, 0.3f}, Uv{0.9f, 0.6f}}) {
        check(near(uvFor(perFrame(still, dt), p, 1.0f, 0.0f), uvFor(perFrame(still, dt), p, 1.0f, 9.0f), 1e-6f),
              "warp speed 0 holds the pattern still");
        const Uv half{0.5f + (p.u - 0.5f) * 0.5f, 0.5f + (p.v - 0.5f) * 0.5f};
        const Uv wide = uvFor(perFrame(broad, dt), p, 1.0f, 0.0f);
        const Uv narrow = uvFor(perFrame(still, dt), half, 1.0f, 0.0f);
        check(near(wide.u - p.u, narrow.u - half.u, 1e-6f) && near(wide.v - p.v, narrow.v - half.v, 1e-6f),
              "warp scale 2 is the same pattern twice as broad");
    }
}

Uv travel(const Motion& m, Uv p, float aspect, float hz, float seconds) {
    const float dt = 1.0f / hz;
    const FrameMotion f = perFrame(m, dt);
    const int n = static_cast<int>(std::lround(seconds * hz));
    for (int i = 0; i < n; ++i) p = uvFor(f, p, aspect, 0.3f + static_cast<float>(i) * dt);
    return p;
}

void sameTravelAtAnyFrameRate() {
    Motion m;
    m.zoom = 1.6f;
    m.zoomExp = 1.3f;
    m.rot = 0.4f;
    m.cx = 0.55f;
    m.cy = 0.45f;
    m.dx = 0.03f;
    m.dy = -0.02f;
    m.sx = 1.1f;
    m.sy = 0.95f;
    m.warp = 1.0f;
    for (float a : {1.0f, 9.0f / 16.0f}) {
        for (Uv p : {Uv{0.2f, 0.3f}, Uv{0.5f, 0.5f}, Uv{0.8f, 0.7f}, Uv{0.1f, 0.9f}}) {
            const Uv at120 = travel(m, p, a, 120.0f, 1.0f);
            check(near(at120, travel(m, p, a, 60.0f, 1.0f), 3e-3f), "a second at 60 fps travels as far as at 120");
            check(near(at120, travel(m, p, a, 30.0f, 1.0f), 6e-3f), "a second at 30 fps travels as far as at 120");
        }
        Motion zoomOnly;
        zoomOnly.zoom = 3.0f;
        const Uv p{0.9f, 0.2f};
        check(near(travel(zoomOnly, p, a, 120.0f, 1.0f), travel(zoomOnly, p, a, 30.0f, 1.0f), 1e-4f),
              "a plain zoom compounds exactly at any frame rate");
    }
}

void silenceIsStill() {
    Motion base;
    base.zoom = 3.0f;
    base.rot = 0.5f;
    base.dx = 0.1f;
    base.sx = 1.3f;
    base.warp = 1.0f;
    GeodeSoundFrame s = playing(2.0f);
    s.presence = 0.0f;
    const Motion m = fromSound(base, s);
    check(m.zoom == 1.0f && m.rot == 0.0f && m.dx == 0.0f && m.sx == 1.0f && m.warp == 0.0f, "no sound, no movement");
    const FrameMotion f = perFrame(m, 1.0f / 60.0f);
    for (Uv p : {Uv{0.1f, 0.2f}, Uv{0.7f, 0.9f}}) {
        check(near(uvFor(f, p, 1.0f, 5.0f), p, 1e-5f), "silence samples where it draws");
    }
}

void bassZoomsTheMilkDropWay() {
    Motion base;
    base.zoom = 2.0f;
    const float atMean = fromSound(base, playing(1.0f)).zoom;
    const float onHit = fromSound(base, playing(2.0f)).zoom;
    const float inDip = fromSound(base, playing(0.4f)).zoom;
    check(near(atMean, 2.0f, 1e-5f), "bass at its mean leaves the base zoom");
    check(onHit > atMean, "bass above its mean zooms in harder");
    check(inDip < atMean && inDip > 1.0f, "bass below its mean eases the zoom");
    check(near(std::log2(onHit), 1.0f + kBassZoom, 1e-4f), "a full drive spends the whole push");

    check(fromSound(base, playing(2.0f, 0.0f)).zoom < onHit, "quiet music pushes less than loud");
    check(fromSound(base, playing(2.0f), true).zoom < onHit, "reduced motion pushes less");

    const float out = fromSound(Motion{}, playing(0.0f)).zoom;
    check(out < 1.0f && out >= std::exp2(-kMaxZoomOutOctaves), "with no base a bass dip backs out, within its limit");
    Motion wild;
    wild.zoom = 1000.0f;
    check(fromSound(wild, playing(3.0f)).zoom <= std::exp2(kMaxZoomInOctaves) + 1e-4f, "zoom in stays within its limit");
}

void nothingNonFiniteGetsThrough() {
    const float nan = std::nanf("");
    const float inf = INFINITY;
    Motion bad;
    bad.zoom = nan;
    bad.zoomExp = inf;
    bad.rot = nan;
    bad.cx = -inf;
    bad.dx = nan;
    bad.sx = -1.0f;
    bad.warp = inf;
    bad.warpScale = 0.0f;
    const Motion clean = sanitize(bad);
    check(clean.zoom == 1.0f && clean.rot == 0.0f && clean.dx == 0.0f && clean.sx == 1.0f, "non-finite falls back to neutral");
    check(std::isfinite(clean.zoomExp) && std::isfinite(clean.cx) && std::isfinite(clean.warp) && clean.warpScale > 0.0f,
          "every field comes out finite");

    GeodeSoundFrame s = playing(nan);
    s.midAtt = inf;
    s.trebleAtt = nan;
    s.drive = nan;
    const Motion heard = fromSound(bad, s);
    check(heard.zoom == 1.0f && std::isfinite(heard.rot) && std::isfinite(heard.warp), "a broken sound frame moves nothing wild");
    GeodeSoundFrame lost{};
    lost.presence = nan;
    check(fromSound(Motion{}, lost).zoom == 1.0f, "an unknown presence is silence");

    const FrameMotion f = perFrame(clean, nan);
    check(f.zoom == 1.0f && f.rot == 0.0f && f.warp == 0.0f, "a non-finite dt moves nothing");
    check(finite(uvFor(perFrame(clean, 1.0f / 60.0f), {nan, 0.5f}, 1.0f, 0.0f)), "a non-finite uv comes back finite");
    check(finite(uvFor(perFrame(clean, 1.0f / 60.0f), {0.3f, 0.5f}, nan, nan)), "a non-finite aspect or time is harmless");
    FrameMotion broken;
    broken.sx = 0.0f;
    check(finite(uvFor(broken, {0.3f, 0.5f}, 1.0f, 0.0f)), "a hand-built broken frame cannot poison the sample");
    check(std::isfinite(fold(nan)) && std::isfinite(fold(inf)), "the fold takes anything");
}

void mirrorFoldReflectsTheEdges() {
    for (float p : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.99f, 1.0f}) {
        check(near(fold(p), p, 1e-6f), "inside 0..1 the fold is the identity");
    }
    const float e = 1e-3f;
    check(near(fold(1.0f + e), fold(1.0f - e), 1e-5f), "the fold is continuous at the right edge");
    check(near(fold(-e), fold(e), 1e-5f), "the fold is continuous at the left edge");
    check(near(fold(1.25f), 0.75f, 1e-6f) && near(fold(-0.25f), 0.25f, 1e-6f), "past an edge the picture reflects");
    check(near(fold(2.25f), 0.25f, 1e-6f) && near(fold(-1.75f), 0.25f, 1e-6f), "the fold repeats every two");
    for (float p = -5.0f; p <= 5.0f; p += 0.037f) {
        const float q = fold(p);
        check(q >= 0.0f && q <= 1.0f, "the fold always lands inside 0..1");
    }
    Motion out;
    out.zoom = 0.5f;
    const Uv corner = fold(uvFor(perFrame(out, kMaxDt), {0.0f, 1.0f}, 16.0f / 9.0f, 0.0f));
    check(corner.u >= 0.0f && corner.u <= 1.0f && corner.v >= 0.0f && corner.v <= 1.0f,
          "zooming out folds the corners back inside");
}

// The renderer's glue (Renderer::drawDive): what it asks for, heard from a sound frame, stepped at 30,
// 60 and 120 fps.
Uv travelHeard(const DiveAsk& ask, const GeodeSoundFrame& s, Uv p, float hz, float seconds) {
    return travel(diveMotion(ask, s, false), p, 9.0f / 16.0f, hz, seconds);
}

void theRendererDivesBothWays() {
    DiveAsk in;
    in.octaves = 0.6f;
    DiveAsk out;
    out.octaves = -0.6f;
    const GeodeSoundFrame s = playing(1.0f, 1.0f);
    check(near(std::log2(diveMotion(in, s, false).zoom), 0.6f, 1e-4f), "Dive speed at the bass's mean is its zoom, octaves a second");
    check(near(std::log2(diveMotion(out, s, false).zoom), -0.6f, 1e-4f), "a negative Fly backs out by feedback too");
    check(near(travelOctaves(diveMotion(out, s, false)), 0.6f, 1e-4f), "the pour is timed by travel either way");
    const Uv p{0.8f, 0.3f};
    for (float hz : {30.0f, 60.0f, 120.0f}) {
        const Uv a = travelHeard(in, s, p, hz, 1.0f);
        check(std::fabs(a.u - 0.5f) < std::fabs(p.u - 0.5f), "diving in samples toward the centre at any rate");
        check(near(a, travelHeard(in, s, p, 120.0f, 1.0f), 1e-4f), "a second of the dive lands alike at 30, 60 and 120 fps");
        const Uv b = travelHeard(out, s, p, hz, 1.0f);
        check(std::fabs(b.u - 0.5f) > std::fabs(p.u - 0.5f), "backing out samples past the start, toward the edge");
        const Uv f = fold(b);
        check(f.u >= 0.0f && f.u <= 1.0f && f.v >= 0.0f && f.v <= 1.0f, "and the fold keeps it on the picture");
    }
    DiveAsk wild;
    wild.octaves = 50.0f;
    check(std::log2(diveBase(wild).zoom) <= kMaxDiveOctaves + 1e-5f, "Dive speed's ceiling holds before the bass");
    check(diveBase(DiveAsk{}).zoom == 1.0f && !asksDive(DiveAsk{}), "nothing asked, nothing moves");
}

void theBassPushesOnlyADive() {
    DiveAsk dive;
    dive.octaves = 0.5f;
    DiveAsk trails;
    trails.sustain = true;
    trails.trailZoom = 0.1f;
    trails.trailWarp = 0.6f;
    const Motion t = diveMotion(trails, playing(2.5f, 1.0f), false);
    check(near(std::log2(t.zoom), 0.1f * kTrailZoomOctaves, 1e-4f), "Trails alone keep their echo zoom whatever the bass");
    check(t.rot == 0.0f && near(t.warp, 0.6f, 1e-6f), "Trail warp is MilkDrop's warp, unpushed");
    check(diveMotion(dive, playing(2.5f, 1.0f), false).zoom > diveMotion(dive, playing(1.0f, 1.0f), false).zoom,
          "a dive surges on the bass");
    DiveAsk back;
    back.sustain = true;
    back.trailZoom = -0.5f;
    check(std::log2(diveBase(back).zoom) >= -kMaxZoomOutOctaves - 1e-5f, "a shrinking echo backs out within the limit");
}

void theRendererIsStillInSilence() {
    DiveAsk ask;
    ask.octaves = 1.0f;
    ask.sustain = true;
    ask.trailZoom = 0.3f;
    ask.trailWarp = 1.0f;
    GeodeSoundFrame quiet = playing(2.0f);
    quiet.presence = 0.0f;
    const Motion m = diveMotion(ask, quiet, false);
    check(m.zoom == 1.0f && m.rot == 0.0f && m.warp == 0.0f, "silence: the dive and the echo stop");
    for (float hz : {30.0f, 60.0f, 120.0f}) {
        const Uv p{0.15f, 0.85f};
        check(near(uvFor(perFrame(m, 1.0f / hz), p, 1.0f, 2.0f), p, 1e-6f), "silence samples where it draws at any rate");
    }
}

// The old liquid_frag back-trace (before lib_feedback): rotate by -twist about the vanishing point in
// centred units (x times the aspect, y up) and divide by the scale.
Uv oldLiquidBackTrace(const liquid::DiveFrame& d, Uv uv, float aspect) {
    const float cx = (uv.u * 2.0f - 1.0f) * aspect;
    const float cy = uv.v * 2.0f - 1.0f;
    float dx = cx - d.vpX;
    float dy = cy - d.vpY;
    const float s = std::sin(-d.twist);
    const float c = std::cos(-d.twist);
    const float rx = (c * dx - s * dy) / d.scale;
    const float ry = (s * dx + c * dy) / d.scale;
    return {((d.vpX + rx) / aspect) * 0.5f + 0.5f, (d.vpY + ry) * 0.5f + 0.5f};
}

void liquidDiveIsMilkDropsTransform() {
    for (float a : kAspects) {
        for (liquid::DiveFrame d : {liquid::DiveFrame{1.02f, 0.0f, 0.0f, 0.0f}, liquid::DiveFrame{1.03f, 0.01f, 0.2f, -0.1f},
                                    liquid::DiveFrame{1.01f, -0.008f, -0.15f, 0.18f}}) {
            const FrameMotion f = fromDive(d, a);
            for (Uv p : {Uv{0.1f, 0.2f}, Uv{0.5f, 0.5f}, Uv{0.9f, 0.7f}, Uv{0.3f, 0.95f}}) {
                check(near(uvFor(f, p, a, 4.0f), oldLiquidBackTrace(d, p, a), 2e-6f),
                      "Liquid's dive through MilkDrop's transform lands where the old back-trace did");
            }
        }
    }
    const FrameMotion still = fromDive(liquid::DiveFrame{}, 1.0f);
    check(near(uvFor(still, {0.3f, 0.6f}, 1.0f, 0.0f), Uv{0.3f, 0.6f}, 1e-6f), "a still dive is the identity");
    const FrameMotion bad = fromDive(liquid::DiveFrame{std::nanf(""), INFINITY, std::nanf(""), 0.0f}, std::nanf(""));
    check(finite(uvFor(bad, {0.3f, 0.6f}, 1.0f, 0.0f)), "a broken dive cannot poison the sample");
}

}  // namespace

int main() {
    neutralIsIdentity();
    zoomMovesTowardAndAwayFromTheCentre();
    zoomExpShapesTheZoomByRadius();
    rotationAboutAnOffCentrePoint();
    translationAndStretch();
    warpIsBoundedAndBoils();
    sameTravelAtAnyFrameRate();
    silenceIsStill();
    bassZoomsTheMilkDropWay();
    nothingNonFiniteGetsThrough();
    mirrorFoldReflectsTheEdges();
    theRendererDivesBothWays();
    theBassPushesOnlyADive();
    theRendererIsStillInSilence();
    liquidDiveIsMilkDropsTransform();
    if (failures > 0) {
        std::printf("%d feedback motion check(s) failed\n", failures);
        return 1;
    }
    std::printf("feedback motion: all checks passed\n");
    return 0;
}
