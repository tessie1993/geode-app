#include "viz/pool/PoolMath.hpp"

#include <algorithm>
#include <cmath>


namespace geode::viz::pool {
namespace {

constexpr float kTwoPi = 6.28318530718f;
// Deep-water dispersion, omega = c * sqrt(k): long waves travel slowly, short ones a little faster.
constexpr float kDispersion = 0.6f;
constexpr float kWakeRiseSeconds = 0.15f;
constexpr float kWanderRate = 0.11f;

struct WaveShape {
    float angle;
    float k;
    float weight;
};
// Four long swell waves, then four short ones that mostly show in the caustics. Each group's weights sum to 1.
constexpr std::array<WaveShape, kWaves> kShapes{{
    {1.35f, 2.3f, 0.40f},  {2.30f, 3.1f, 0.28f},  {0.55f, 4.4f, 0.20f},  {3.90f, 5.9f, 0.12f},
    {0.90f, 11.0f, 0.34f}, {2.80f, 15.0f, 0.28f}, {4.45f, 19.0f, 0.22f}, {5.95f, 24.0f, 0.16f},
}};

struct RingShape {
    float x;  // from the point the eye looks at
    float y;
    float weight;
    float offset;
};
constexpr std::array<RingShape, kRings> kRingShapes{{
    {-0.55f, -0.35f, 0.40f, 0.0f},
    {0.65f, 0.25f, 0.35f, 2.1f},
    {-0.10f, 0.95f, 0.25f, 4.3f},
}};
constexpr float kRingWander = 0.35f;

float smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }
float unit(float v, float fallback) { return std::clamp(finiteOr(v, fallback), 0.0f, 1.0f); }
float knob(const Controls& c, int i) { return unit(c.knobs[static_cast<size_t>(i)], 0.5f) - 0.5f; }

float waveGain(const Gains& g, int i) { return i < kSwellWaves ? g.swell : g.chop; }

// The ring's radial profile f(s) = a sin(k s - phase) w(s), w the Gaussian window, and its first two
// derivatives in s, where s = sqrt(r^2 + soft^2).
struct Radial {
    float f;
    float d1;
    float d2;
};
Radial ringRadial(const Ring& ring, float amp, float s) {
    const float inv = 1.0f / (kRingReach * kRingReach);
    const float w = std::exp(-0.5f * s * s * inv);
    const float w1 = -s * inv * w;
    const float w2 = (s * s * inv * inv - inv) * w;
    const float th = kRingK * s - ring.phase;
    const float sn = std::sin(th);
    const float cs = std::cos(th);
    return {amp * sn * w, amp * (kRingK * cs * w + sn * w1), amp * (-kRingK * kRingK * sn * w + 2.0f * kRingK * cs * w1 + sn * w2)};
}

struct WakeTerm {
    float e;
    float u;
    float inv2;
};
WakeTerm wakeTerm(const Wake& w, float x, float y) {
    const float inv2 = 1.0f / (w.sigma * w.sigma);
    const float u = ((x - w.x) * (x - w.x) + (y - w.y) * (y - w.y)) * inv2;
    return {std::exp(-0.5f * u), u, inv2};
}

}  // namespace

float height(const Surface& s, const Gains& g, float x, float y) {
    float h = 0.0f;
    for (int i = 0; i < kWaves; i++) {
        const Wave& w = s.waves[static_cast<size_t>(i)];
        h += waveGain(g, i) * w.amp * std::sin(w.kx * x + w.ky * y - w.phase);
    }
    for (const Ring& r : s.rings) {
        const float d = std::sqrt((x - r.x) * (x - r.x) + (y - r.y) * (y - r.y) + kRingSoft * kRingSoft);
        h += ringRadial(r, g.rings * r.amp, d).f;
    }
    for (const Wake& w : s.wakes) {
        if (w.amp <= 0.0f) continue;
        const WakeTerm t = wakeTerm(w, x, y);
        h -= w.amp * (1.0f - 0.5f * t.u) * t.e;
    }
    return h;
}

std::array<float, 2> gradient(const Surface& s, const Gains& g, float x, float y) {
    float gx = 0.0f;
    float gy = 0.0f;
    for (int i = 0; i < kWaves; i++) {
        const Wave& w = s.waves[static_cast<size_t>(i)];
        const float c = waveGain(g, i) * w.amp * std::cos(w.kx * x + w.ky * y - w.phase);
        gx += c * w.kx;
        gy += c * w.ky;
    }
    for (const Ring& r : s.rings) {
        const float dx = x - r.x;
        const float dy = y - r.y;
        const float d = std::sqrt(dx * dx + dy * dy + kRingSoft * kRingSoft);
        const float k = ringRadial(r, g.rings * r.amp, d).d1 / d;
        gx += k * dx;
        gy += k * dy;
    }
    for (const Wake& w : s.wakes) {
        if (w.amp <= 0.0f) continue;
        const WakeTerm t = wakeTerm(w, x, y);
        const float d = -w.amp * t.e * (0.25f * t.u - 1.0f) * 2.0f * t.inv2;
        gx += d * (x - w.x);
        gy += d * (y - w.y);
    }
    return {gx, gy};
}

Hessian hessian(const Surface& s, const Gains& g, float x, float y) {
    Hessian h{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < kWaves; i++) {
        const Wave& w = s.waves[static_cast<size_t>(i)];
        const float c = -waveGain(g, i) * w.amp * std::sin(w.kx * x + w.ky * y - w.phase);
        h.xx += c * w.kx * w.kx;
        h.xy += c * w.kx * w.ky;
        h.yy += c * w.ky * w.ky;
    }
    for (const Ring& r : s.rings) {
        const float dx = x - r.x;
        const float dy = y - r.y;
        const float d = std::sqrt(dx * dx + dy * dy + kRingSoft * kRingSoft);
        const Radial f = ringRadial(r, g.rings * r.amp, d);
        const float radial = (f.d2 - f.d1 / d) / (d * d);
        const float iso = f.d1 / d;
        h.xx += radial * dx * dx + iso;
        h.xy += radial * dx * dy;
        h.yy += radial * dy * dy + iso;
    }
    for (const Wake& w : s.wakes) {
        if (w.amp <= 0.0f) continue;
        const WakeTerm t = wakeTerm(w, x, y);
        const float d1 = -w.amp * t.e * (0.25f * t.u - 1.0f) * 2.0f * t.inv2;
        const float d2 = -w.amp * t.e * (0.75f - 0.125f * t.u) * 4.0f * t.inv2 * t.inv2;
        const float dx = x - w.x;
        const float dy = y - w.y;
        h.xx += d2 * dx * dx + d1;
        h.xy += d2 * dx * dy;
        h.yy += d2 * dy * dy + d1;
    }
    return h;
}

std::array<float, 2> refraction(const Surface& s, const Gains& g, float x, float y) {
    const auto grad = gradient(s, g, x, y);
    float ox = -grad[0] * s.look.refraction;
    float oy = -grad[1] * s.look.refraction;
    const float len = std::sqrt(ox * ox + oy * oy);
    if (len > 1.0e-6f) {
        const float capped = kMaxRefraction * std::tanh(len / kMaxRefraction);
        ox *= capped / len;
        oy *= capped / len;
    }
    return {ox, oy};
}

float causticLuma(const Surface& s, const Gains& g, float x, float y) {
    const Hessian h = hessian(s, g, x, y);
    const float d = s.look.focus;
    // How much the surface above p gathers the light: 1 / |det| of the refracted rays' spread.
    const float det = 1.0f + d * (h.xx + h.yy) + d * d * (h.xx * h.yy - h.xy * h.xy);
    const float gain = 1.0f / std::max(std::fabs(det), kFocusFloor) - 1.0f;
    const float shaped = gain > 0.0f ? gain / (1.0f / kFocusFloor - 1.0f) : s.look.shade * gain;
    return s.look.caustics * shaped;
}

std::optional<std::array<float, 2>> surfacePoint(const View& v, float sx, float sy) {
    // forward (0, sin, -cos), up (0, cos, sin), right (1, 0, 0)
    const float dx = sx * v.fov;
    const float dy = v.sinTilt + sy * v.fov * v.cosTilt;
    const float dz = -v.cosTilt + sy * v.fov * v.sinTilt;
    if (!(dz < -kHorizon)) return std::nullopt;
    const float t = v.height / -dz;
    return std::array<float, 2>{dx * t, dy * t};
}

float fastestHz(float speed) {
    const float pace = std::clamp(finiteOr(speed, 1.0f), 0.0f, kMaxSpeed);
    float omega = kRingOmega;
    for (const WaveShape& w : kShapes) omega = std::max(omega, kDispersion * std::sqrt(w.k));
    return omega * pace / kTwoPi;
}

View viewFor(const Controls& c) {
    const float tilt = std::clamp(0.85f - 0.70f * unit(c.morph, 0.0f) + 0.30f * knob(c, 5), 0.10f, 0.95f);
    return {std::sin(tilt), std::cos(tilt), kEyeHeight, kFieldOfView};
}

float wakeSigma(float age) { return kWakeSigma0 + kWakeSpread * std::max(age, 0.0f); }

float wakeAmp(float strength, float age) {
    if (!(age >= 0.0f) || age >= kWakeLifeSeconds) return 0.0f;
    const float rise = smooth01(age / kWakeRiseSeconds);
    const float fall = (kWakeSigma0 / wakeSigma(age)) * std::exp(-age / kWakeTauSeconds) *
                       (1.0f - smooth01((age - 0.8f * kWakeLifeSeconds) / (0.2f * kWakeLifeSeconds)));
    return kWakeHeight * std::clamp(strength, 0.0f, 1.0f) * rise * fall;
}

void Pool::reset() { *this = Pool(); }

int Pool::wakeCount() const {
    return static_cast<int>(std::count_if(wakes_.begin(), wakes_.end(), [](const Live& w) { return w.strength > 0.0f; }));
}

void Pool::spawn(float x, float y, float strength) {
    Live* slot = &wakes_[0];
    float weakest = 1.0e9f;
    for (Live& w : wakes_) {
        // After its rise a wake only falls, so the one lowest now is the one least missed.
        const float now = w.strength > 0.0f ? wakeAmp(w.strength, std::max(w.age, kWakeRiseSeconds)) : -1.0f;
        if (now < weakest) {
            weakest = now;
            slot = &w;
        }
    }
    *slot = {x, y, 0.0f, strength};
}

void Pool::step(float dt, const Controls& c, const float* fingers, int fingerCount, float aspect) {
    const float frame = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f;
    const float clock = frame * std::clamp(finiteOr(c.speed, 1.0f), 0.0f, kMaxSpeed);
    for (int i = 0; i < kWaves; i++) {
        const size_t n = static_cast<size_t>(i);
        phase_[n] = std::fmod(phase_[n] + kDispersion * std::sqrt(kShapes[n].k) * clock, kTwoPi);
    }
    for (float& p : ringPhase_) p = std::fmod(p + kRingOmega * clock, kTwoPi);
    wander_ = std::fmod(wander_ + kWanderRate * clock, kTwoPi * 100.0f);
    for (Live& w : wakes_) {
        if (w.strength <= 0.0f) continue;
        w.age += frame;
        if (w.age >= kWakeLifeSeconds) w = Live();
    }
    // A still clock ages nothing, so a touch in silence leaves no wake to appear later.
    const bool moving = clock > 0.0f;
    const int count = fingers ? std::clamp(fingerCount, 0, kMaxFingers) : 0;
    const float wide = std::isfinite(aspect) && aspect > 0.0f ? aspect : 1.0f;
    const View view = viewFor(c);
    for (int i = 0; i < kMaxFingers; i++) {
        Finger& fg = fingers_[static_cast<size_t>(i)];
        if (i >= count) {
            fg.down = false;
            continue;
        }
        const float sx = fingers[i * 2];
        const float sy = fingers[i * 2 + 1];
        if (!std::isfinite(sx) || !std::isfinite(sy)) continue;
        const auto at = surfacePoint(view, sx * wide, sy);
        if (!at) {
            fg.down = false;
            continue;
        }
        const float x = (*at)[0];
        const float y = (*at)[1];
        if (!fg.down) {
            fg = {x, y, 0.0f, true};
            if (moving) spawn(x, y, 1.0f);
            continue;
        }
        fg.since += frame;
        if (moving && std::hypot(x - fg.x, y - fg.y) >= kWakeSpacing && fg.since >= kWakeMinInterval) {
            fg = {x, y, 0.0f, true};
            spawn(x, y, 0.6f);
        }
    }
}

Surface Pool::surface(const Controls& c) const {
    Surface s;
    s.view = viewFor(c);
    const float swell = 0.75f + 0.5f * knob(c, 2);                            // k2 FLOW
    const float chop = std::clamp(0.5f + 0.5f * finiteOr(c.turbulence, 0.0f) / 1.5f, 0.0f, 1.0f) *
                       (0.75f + 0.5f * knob(c, 1));                                // k1 TEXTURE
    const float rings = 0.75f + 0.5f * knob(c, 0);                                       // k0 PULSE
    for (int i = 0; i < kWaves; i++) {
        const size_t n = static_cast<size_t>(i);
        const WaveShape& shape = kShapes[n];
        const float amp = i < kSwellWaves ? kSwellHeight * swell : kChopHeight * chop;
        s.waves[n] = {shape.k * std::cos(shape.angle), shape.k * std::sin(shape.angle), phase_[n], amp * shape.weight};
    }
    // The sources wander around the point the eye looks at, so a slant and a view from above both see them.
    const auto centre = surfacePoint(s.view, 0.0f, 0.0f).value_or(std::array<float, 2>{0.0f, 1.0f});
    for (int i = 0; i < kRings; i++) {
        const size_t n = static_cast<size_t>(i);
        const RingShape& r = kRingShapes[n];
        const float a = wander_ * (1.0f + 0.37f * static_cast<float>(i)) + r.offset;
        s.rings[n] = {centre[0] + r.x + kRingWander * std::sin(a), centre[1] + r.y + kRingWander * std::cos(0.8f * a),
                      ringPhase_[n] + r.offset, kRingHeight * r.weight * rings};
    }
    for (int i = 0; i < kMaxWakes; i++) {
        const size_t n = static_cast<size_t>(i);
        const Live& w = wakes_[n];
        const float amp = w.strength > 0.0f ? wakeAmp(w.strength, w.age) : 0.0f;
        s.wakes[n] = {w.x, w.y, wakeSigma(w.age), amp};
    }
    const float depth = unit(c.warp, 0.0f);
    s.look.refraction = 0.5f + 1.5f * depth;                                           // Warp: deeper water
    s.look.focus = std::max(3.0f + 3.0f * depth + 2.0f * knob(c, 4), 0.2f);         // k4 BUILD
    const float bright = std::clamp(0.35f + 0.45f * finiteOr(c.intensity, 1.0f), 0.0f, 1.0f);
    s.look.caustics = kCausticCeiling * bright;
    s.look.shade = kCausticShade;
    return s;
}

}  // namespace geode::viz::pool
