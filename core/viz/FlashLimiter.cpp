#include "viz/FlashLimiter.hpp"

#include <algorithm>
#include <cmath>

#include "viz/VisualSafety.hpp"

namespace geode::viz::flash_limit {

using namespace safety;

float quantise(float v) { return std::floor(std::clamp(v, 0.0f, 1.0f) * kQuantSteps + 0.5f) / kQuantSteps; }

float quantiseAge(float age) {
    return std::floor(std::clamp(age / kEmptyAge, 0.0f, 1.0f) * kQuantSteps) / kQuantSteps * kEmptyAge;
}

float srgbToLinear(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

float luminance(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

namespace {
// R / (R + G + B) >= kRedSaturation, written without the division (kRedSaturation = 0.8: R >= 4(G + B)).
bool saturatedRed(float r, float g, float b) { return r > 0.0f && r * (1.0f - kRedSaturation) >= kRedSaturation * (g + b); }
}  // namespace

void guard(float& r, float& g, float& b) {
    const float excess = r * (1.0f - kRedGuardRatio) - kRedGuardRatio * (g + b);
    if (!(excess > 0.0f)) return;
    const float y = luminance(r, g, b);
    const float s = std::min(1.0f, excess / (excess + (3.0f * kRedGuardRatio - 1.0f) * y));
    r += s * (y - r);
    g += s * (y - g);
    b += s * (y - b);
}

PixelMeasure measure(float r, float g, float b) {
    PixelMeasure m;
    m.lum = luminance(r, g, b);
    m.red = saturatedRed(r, g, b) ? r - g - b : 0.0f;
    float gr = r, gg = g, gb = b;
    guard(gr, gg, gb);
    m.guardedLum = luminance(gr, gg, gb);
    return m;
}

Layout Layout::forSize(int width, int height) {
    Layout l;
    const bool landscape = width >= height;
    l.cols = landscape ? kFlashGridLong : kFlashGridShort;
    l.rows = landscape ? kFlashGridShort : kFlashGridLong;
    return l;
}

namespace {
// Three windows along an axis of n tiles: size ceil(n / 2), at 0, the middle and the end.
void axisWindow(int n, int k, int& start, int& size) {
    size = (n + 1) / 2;
    start = k == 0 ? 0 : k == 1 ? (n - size) / 2 : n - size;
}
}  // namespace

void Layout::region(int r, int& x0, int& y0, int& w, int& h) const {
    axisWindow(cols, r % 3, x0, w);
    axisWindow(rows, r / 3, y0, h);
}

bool Layout::inRegion(int tile, int r) const {
    int x0, y0, w, h;
    region(r, x0, y0, w, h);
    const int x = tile % cols;
    const int y = tile / cols;
    return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
}

int eventsInWindow(const Track& track) {
    int n = 0;
    for (const float age : track.ages) n += age <= kFlashWindow + kFlashWindowSlack ? 1 : 0;
    return n;
}

namespace {

struct Probe {
    bool flip = false;    // the leg turns: a transition is confirmed
    bool counts = false;  // ...and it is a counted one (or a pending fall just dipped under the dark limit)
};

// The zigzag detector, read-only: what showing `v` would do. A transition is confirmed when the
// shown value has come back a full swing from the leg's extreme. Luminance transitions count only if
// the darker side is under the dark limit; a fall confirmed above it waits (pending) and is counted
// the moment the same leg dips under it.
//
// Why at most kSlots events in a window means at most kSlots + 1 transitions in it. Take any chain
// of shown values v(i0), v(i1), ..., v(ik) inside the window whose consecutive differences alternate
// in sign and are each a full swing. Call the detector "aligned" at i(l) when its leg runs the way
// chain leg l ran. If it is aligned at i(l), its extreme is at least as far out as v(i(l)), so by
// i(l+1), a full swing the other way, it has confirmed a turn: at least one event in
// (i(l), i(l+1)]. If it is not aligned, it is already running the way leg l+1 goes, and at i(l+1)
// it is aligned unless it turned an odd number of times on the way. So with f(l) the events in
// (i0, i(l)], f(l) >= l - 1 when aligned at i(l) and f(l) >= l when not, by induction from
// f(0) = 0; hence f(k) >= k - 1, and the chain has at most (events in the window) + 1 legs.
// core/tests/flash_limit_test.cpp checks the bound with the dark-limit rule included, by brute force.
Probe probe(const Track& t, float v, bool lum) {
    const float theta = (lum ? kFlashSwing : kRedSwing) - kFlashDetectSlack;
    const float dark = kFlashDarkLimit + kFlashDetectSlack;
    Probe p;
    if (t.up) {
        if (v < t.extreme && t.extreme - v >= theta) {
            p.flip = true;
            p.counts = !lum || v < dark;
        }
        return p;
    }
    if (v <= t.extreme) {
        p.counts = lum && t.pending && v < dark;
    } else if (v - t.extreme >= theta) {
        p.flip = true;
        p.counts = !lum || t.extreme < dark;
    }
    return p;
}

// Whether the limiter may let this frame's value through for this track.
bool allowed(const Track& t, const Probe& p, bool lum) {
    if (!p.counts) return true;
    const int events = eventsInWindow(t);
    if (lum) return events + 1 <= kSlots;
    // Red: a rise needs room for the fall that must follow it, since the limiter can take red away
    // but cannot put it back; so a fall always finds its slot.
    return !p.flip || t.up || events + 2 <= kSlots;
}

void record(Track& t) {
    int oldest = 0;
    for (int i = 1; i < kSlots; ++i) {
        if (t.ages[i] > t.ages[oldest]) oldest = i;
    }
    t.ages[oldest] = 0.0f;
}

void commit(Track& t, float v, bool lum) {
    const Probe p = probe(t, v, lum);
    if (p.flip) {
        t.up = !t.up;
        t.extreme = quantise(v);
        t.pending = lum && !t.up && !p.counts;
    } else if (t.up ? v > t.extreme : v < t.extreme) {
        t.extreme = quantise(v);
        if (p.counts) t.pending = false;
    }
    if (p.counts) record(t);
}

void prime(Track& t, float v) {
    t.up = false;
    t.pending = false;
    t.extreme = quantise(v);
    t.ages.fill(kEmptyAge);
}

}  // namespace

FrameResult step(State& state, const Layout& layout, const std::array<TileInput, kTiles>& in, float dt) {
    const float stepDt = std::clamp(dt, 0.0f, kFlashMaxDt);
    std::array<float, kTiles> lumIn{}, redIn{}, guardedIn{}, shown{}, red{};
    for (int t = 0; t < kTiles; ++t) {
        lumIn[t] = quantise(in[t].lum);
        redIn[t] = quantise(in[t].red);
        guardedIn[t] = quantise(in[t].guardedLum);
    }
    std::array<std::array<bool, kTiles>, kRegions> member{};
    for (int r = 0; r < kRegions; ++r) {
        for (int t = 0; t < kTiles; ++t) member[r][t] = layout.inRegion(t, r);
    }
    FrameResult result;
    const auto regionMeans = [&](const std::array<float, kTiles>& lum, const std::array<float, kTiles>& rd) {
        for (int r = 0; r < kRegions; ++r) {
            float area = 0.0f, sumLum = 0.0f, sumRed = 0.0f;
            for (int t = 0; t < kTiles; ++t) {
                if (!member[r][t]) continue;
                area += in[t].area;
                sumLum += in[t].area * lum[t];
                sumRed += in[t].area * rd[t];
            }
            result.lumShown[r] = sumLum / area;
            result.redShown[r] = sumRed / area;
        }
    };

    if (!state.primed) {
        // The first frame after create, a surface change or a turn is shown as drawn.
        for (int t = 0; t < kTiles; ++t) state.tiles[t] = {lumIn[t], false};
        regionMeans(lumIn, redIn);
        for (int r = 0; r < kRegions; ++r) {
            prime(state.lum[r], result.lumShown[r]);
            prime(state.red[r], result.redShown[r]);
        }
        state.primed = true;
        return result;
    }
    for (auto* tracks : {&state.lum, &state.red}) {
        for (Track& t : *tracks) {
            for (float& age : t.ages) age = quantiseAge(std::min(age + stepDt, kEmptyAge));
        }
    }

    std::array<bool, kTiles> held{}, guarded{};
    const float release = kFlashReleaseRate * stepDt;
    // Every tile's shown luminance and red for the current holds and guards.
    const auto evaluate = [&]() {
        for (int t = 0; t < kTiles; ++t) {
            const TileState& prev = state.tiles[t];
            float base = 0.0f, target = 0.0f;
            for (int pass = 0; pass < 2; ++pass) {
                base = guarded[t] ? guardedIn[t] : lumIn[t];
                target = held[t] ? prev.shown : prev.releasing ? prev.shown + std::clamp(base - prev.shown, -release, release) : base;
                // A tile the limiter touches is guarded first: its red is then exactly 0, where a gain
                // or a lift on saturated pixels would leave an amount its means cannot tell.
                if (target != base && !guarded[t] && redIn[t] > 0.0f) {
                    guarded[t] = true;
                    continue;
                }
                break;
            }
            TileTransform& x = result.transforms[t];
            shown[t] = quantise(target);
            x.guard = guarded[t];
            x.gain = shown[t] < base ? shown[t] / base : 1.0f;
            x.lift = shown[t] > base ? (shown[t] - base) / (1.0f - base) : 0.0f;
            red[t] = guarded[t] ? 0.0f : redIn[t];
        }
        regionMeans(shown, red);
    };

    bool settled = false;
    for (int iteration = 0; iteration < kMaxIterations && !settled; ++iteration) {
        evaluate();
        settled = true;
        for (int r = 0; r < kRegions; ++r) {
            const bool holdLum = !allowed(state.lum[r], probe(state.lum[r], result.lumShown[r], true), true);
            const bool guardRed = !allowed(state.red[r], probe(state.red[r], result.redShown[r], false), false);
            for (int t = 0; t < kTiles; ++t) {
                if (!member[r][t]) continue;
                if (holdLum && !held[t]) {
                    held[t] = true;
                    settled = false;
                }
                if (guardRed && !guarded[t]) {
                    guarded[t] = true;
                    settled = false;
                }
            }
        }
    }
    if (!settled) {
        // Cannot happen (each unsettled pass locks a new region and kind, and there are 18), but if it
        // did, holding and guarding everything changes no region's luminance and only lowers red.
        held.fill(true);
        guarded.fill(true);
        evaluate();
    }

    for (int r = 0; r < kRegions; ++r) {
        commit(state.lum[r], result.lumShown[r], true);
        commit(state.red[r], result.redShown[r], false);
    }
    for (int t = 0; t < kTiles; ++t) {
        const float base = guarded[t] ? guardedIn[t] : lumIn[t];
        TileState& s = state.tiles[t];
        s.releasing = held[t] || (s.releasing && shown[t] != base);
        s.shown = shown[t];
    }
    return result;
}

}  // namespace geode::viz::flash_limit
