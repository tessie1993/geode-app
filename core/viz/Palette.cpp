#include "viz/Palette.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::palette {

namespace {

struct Lab {
    float L, a, b;
};

float toLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float fromLinear(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

Lab toLab(Rgb c) {
    const float r = toLinear(c.r), g = toLinear(c.g), b = toLinear(c.b);
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
            1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

// Linear-light components, unclamped (so a caller can test the gamut).
void toLinearRgb(Lab c, float* out) {
    const float l = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
    const float m = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
    const float s = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
    const float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
    out[0] = 4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
    out[1] = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
    out[2] = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;
}

Rgb fromLab(Lab c) {
    float lin[3];
    toLinearRgb(c, lin);
    return {fromLinear(lin[0]), fromLinear(lin[1]), fromLinear(lin[2])};
}

bool inGamut(Lab c) {
    float lin[3];
    toLinearRgb(c, lin);
    for (float v : lin) {
        if (v < -0.002f || v > 1.002f) return false;
    }
    return true;
}

constexpr float kPi = 3.14159265358979f;

float clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }

float hsvHue(Rgb c) {
    const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
    const float d = mx - mn;
    if (d < 1e-6f) return 0.0f;
    float h;
    if (mx == c.r) {
        h = std::fmod((c.g - c.b) / d, 6.0f);
    } else if (mx == c.g) {
        h = (c.b - c.r) / d + 2.0f;
    } else {
        h = (c.r - c.g) / d + 4.0f;
    }
    h /= 6.0f;
    return h < 0.0f ? h + 1.0f : h;
}

}  // namespace

bool Gradient::operator==(const Gradient& o) const {
    if (count != o.count) return false;
    for (int i = 0; i < count; ++i) {
        const auto k = static_cast<size_t>(i);
        if (stops[k].r != o.stops[k].r || stops[k].g != o.stops[k].g || stops[k].b != o.stops[k].b) return false;
    }
    return true;
}

bool Sequence::operator==(const Sequence& o) const {
    if (beats != o.beats || count != o.count) return false;
    for (int i = 0; i < count; ++i) {
        if (!(palettes[static_cast<size_t>(i)] == o.palettes[static_cast<size_t>(i)])) return false;
    }
    return true;
}

Rgb fromPacked(int32_t rgb) {
    return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(rgb & 0xFF) / 255.0f};
}

int32_t toPacked(Rgb c) {
    const auto byte = [](float v) { return static_cast<int32_t>(std::lround(clamp01(v) * 255.0f)); };
    return (byte(c.r) << 16) | (byte(c.g) << 8) | byte(c.b);
}

Rgb sample(const Gradient& g, float t) {
    if (g.count <= 0) return {};
    if (g.count == 1) return g.stops[0];
    const float x = clamp01(t) * static_cast<float>(g.count - 1);
    const int i = std::min(static_cast<int>(x), g.count - 2);
    const float f = x - static_cast<float>(i);
    const Lab a = toLab(g.stops[static_cast<size_t>(i)]);
    const Lab b = toLab(g.stops[static_cast<size_t>(i) + 1]);
    return fromLab({a.L + (b.L - a.L) * f, a.a + (b.a - a.a) * f, a.b + (b.b - a.b) * f});
}

void bake(const Gradient& g, Baked& out) {
    for (int i = 0; i < kBakedSamples; ++i) {
        const Rgb c = sample(g, static_cast<float>(i) / static_cast<float>(kBakedSamples - 1));
        const auto k = static_cast<size_t>(i) * 3;
        out[k] = c.r;
        out[k + 1] = c.g;
        out[k + 2] = c.b;
    }
}

float oklchHue(Rgb c) {
    const Lab l = toLab(c);
    float h = std::atan2(l.b, l.a) * 180.0f / kPi;
    return h < 0.0f ? h + 360.0f : h;
}

int harmony(Rgb base, Harmony kind, Rgb* out) {
    static constexpr float kOffsets[4][3] = {{180, 0, 0}, {120, 240, 0}, {-30, 30, 0}, {150, 210, 0}};
    static constexpr int kCounts[4] = {1, 2, 2, 2};
    const int k = std::clamp(static_cast<int>(kind), 0, 3);
    const Lab lab = toLab(base);
    const float chroma = std::sqrt(lab.a * lab.a + lab.b * lab.b);
    const float hue = std::atan2(lab.b, lab.a);
    for (int i = 0; i < kCounts[k]; ++i) {
        const float h = hue + kOffsets[k][i] * kPi / 180.0f;
        float c = chroma;
        Lab turned{lab.L, c * std::cos(h), c * std::sin(h)};
        // Pull the chroma in until the colour is real, so the turned hue is the hue that lands.
        for (int step = 0; step < 24 && !inGamut(turned); ++step) {
            c *= 0.92f;
            turned = {lab.L, c * std::cos(h), c * std::sin(h)};
        }
        out[i] = fromLab(turned);
    }
    return kCounts[k];
}

void approximateHues(const Baked& baked, float& base, float& range) {
    const Rgb first{baked[0], baked[1], baked[2]};
    const size_t last = static_cast<size_t>(kBakedSamples - 1) * 3;
    const Rgb end{baked[last], baked[last + 1], baked[last + 2]};
    base = hsvHue(first);
    float span = hsvHue(end) - base;
    if (span < 0.0f) span += 1.0f;
    range = std::clamp(span, 0.02f, 1.0f);
}

namespace {

struct Reader {
    const int32_t* v;
    int n;
    int i = 0;
    bool ok() const { return i < n; }
    int32_t next() { return i < n ? v[i++] : 0; }
};

Gradient readGradient(Reader& r) {
    Gradient g;
    const int declared = r.ok() ? static_cast<int>(r.next()) : 0;
    const int count = std::clamp(declared, 0, kMaxStops);
    const int take = std::min(declared, std::max(0, r.n - r.i));
    for (int s = 0; s < take; ++s) {
        const int32_t rgb = r.next();
        if (s < count) g.stops[static_cast<size_t>(s)] = fromPacked(rgb);
    }
    g.count = count;
    return g;
}

}  // namespace

Wire decodeWire(const int32_t* values, int count) {
    Wire w;
    if (!values || count <= 0) return w;
    Reader r{values, count};
    w.gradient = readGradient(r);
    if (!r.ok()) return w;
    const int beats = static_cast<int>(r.next());
    const int palettes = std::clamp(r.ok() ? static_cast<int>(r.next()) : 0, 0, kMaxSequence);
    w.sequence.beats = std::clamp(beats, 0, 64);
    for (int p = 0; p < palettes; ++p) w.sequence.palettes[static_cast<size_t>(p)] = readGradient(r);
    w.sequence.count = palettes;
    return w;
}

int encodeWire(const Wire& wire, int32_t* out, int capacity) {
    int n = 0;
    const auto put = [&](int32_t v) {
        if (n < capacity) out[n] = v;
        ++n;
    };
    const auto putGradient = [&](const Gradient& g) {
        put(g.count);
        for (int i = 0; i < g.count; ++i) put(toPacked(g.stops[static_cast<size_t>(i)]));
    };
    putGradient(wire.gradient);
    put(wire.sequence.beats);
    put(wire.sequence.count);
    for (int p = 0; p < wire.sequence.count; ++p) putGradient(wire.sequence.palettes[static_cast<size_t>(p)]);
    return n;
}

int SequenceRunner::beatsBetween(float before, float after) {
    float d = std::fmod(after - before, 4096.0f);
    if (d < -2048.0f) d += 4096.0f;
    if (d > 2048.0f) d -= 4096.0f;
    return static_cast<int>(std::lround(d));
}

void SequenceRunner::blend() {
    const float k = mix_ * mix_ * (3.0f - 2.0f * mix_);
    for (size_t i = 0; i < out_.size(); ++i) out_[i] = from_[i] + (to_[i] - from_[i]) * k;
}

void SequenceRunner::set(const Sequence& seq) {
    if (seq == seq_) return;
    seq_ = seq;
    index_ = 0;
    mix_ = 1.0f;
    started_ = false;
    wasLocked_ = false;
    if (active()) {
        bake(seq_.palettes[0], to_);
        from_ = to_;
        out_ = to_;
    }
}

void SequenceRunner::step(float beatCount, float beatPhase, float beatConf, float dt) {
    if (!active()) return;
    const bool locked = std::isfinite(beatCount) && std::isfinite(beatPhase) && beatConf >= kLockedConfidence;
    const double period = static_cast<double>(seq_.beats);
    double delta = std::max(0.0, static_cast<double>(dt)) * kFreeBeatsPerSecond;
    if (locked && wasLocked_) {
        delta = static_cast<double>(beatsBetween(lastCount_, beatCount)) + static_cast<double>(beatPhase - lastPhase_);
        delta = std::clamp(delta, 0.0, 8.0);
    }
    const double target = static_cast<double>(beatCount) + static_cast<double>(beatPhase);
    if (!started_) {
        // Boundaries sit on the rhythm's own counts (a multiple of the cycle), so a 16-beat step lands on a
        // phrase; with no beat the clock starts at zero.
        pos_ = locked ? target : 0.0;
        slot_ = static_cast<long>(std::floor(pos_ / period));
        started_ = true;
    } else {
        pos_ += delta;
        // Back on the beat: line the boundaries up with the rhythm again. The colour is never touched here,
        // only when the next step falls.
        if (locked && !wasLocked_) pos_ = target + period * std::round((pos_ - target) / period);
    }
    if (locked) {
        lastCount_ = beatCount;
        lastPhase_ = beatPhase;
    }
    wasLocked_ = locked;

    if (mix_ < 1.0f) mix_ = std::min(1.0f, mix_ + static_cast<float>(delta) / kFadeBeats);
    const long slot = static_cast<long>(std::floor(pos_ / period));
    if (slot > slot_ && seq_.count > 1) {
        from_ = out_;
        index_ = (index_ + 1) % seq_.count;
        bake(seq_.palettes[static_cast<size_t>(index_)], to_);
        mix_ = 0.0f;
    }
    slot_ = slot;
    blend();
}

}  // namespace geode::viz::palette
