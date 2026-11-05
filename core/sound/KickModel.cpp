#include "sound/KickModel.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace geode::sound {
namespace {

#define GEODE_KICK_MODEL_HEADER
#include "sound/KickModelTable.inc"
#undef GEODE_KICK_MODEL_HEADER

struct Node {
    float v;  // the split's threshold, or the leaf's value
    uint16_t left;
    uint16_t right;
    uint8_t feature;  // 255 = leaf
};

constexpr uint16_t kRoots[] = {
#define GEODE_KICK_MODEL_ROOTS
#include "sound/KickModelTable.inc"
#undef GEODE_KICK_MODEL_ROOTS
};

constexpr Node kNodes[] = {
#define GEODE_KICK_MODEL_NODES
#include "sound/KickModelTable.inc"
#undef GEODE_KICK_MODEL_NODES
};

static_assert(sizeof(kRoots) / sizeof(kRoots[0]) == kKickTrees);
static_assert(sizeof(kNodes) / sizeof(kNodes[0]) == kKickNodes);

// fosfora's band edges, Hz: the bands up to 250 Hz read the 4096-point spectrum, the rest the 1024-point.
constexpr std::array<double, KickModel::kBands + 1> kEdges = {30, 60, 90, 130, 180, 250, 400, 700, 1200, 2500, 5000, 10000};
constexpr double kLargeMaxHz = 250.0;
constexpr double kLowSpanLo = 30.0;  // the span the flatness and peakiness read, on the large spectrum
constexpr double kLowSpanHi = 250.0;
// The magnitude floor before the log in the flux (fosfora's FLUX_FLOOR, at its 2/Σw scaling): bins under it
// all read as the floor, so their change is zero and the flux reads only what rose out of the noise.
constexpr double kFluxFloor = static_cast<double>(2e-3f);  // an f32 in fosfora, widened
constexpr double kDcCutoffHz = 5.0;
constexpr int kMinAnalysisRate = 44100;
constexpr int kTaps = 63;
// The event rule (fosfora's /stem/drums/onset): re-armed once the kick falls under 0.3, fires at 0.5.
constexpr float kRearm = 0.3f;
constexpr float kFire = 0.5f;
constexpr double kMinGap = 0.10;  // s
// fosfora gates the kick to 0 under its -60 LUFS silence line; the sound engine has no momentary loudness,
// so this is its own digital-silence line (SoundEngine.cpp kDigitalSilence, -80 dBFS RMS), over the same
// newest 2048 samples fosfora's RMS reads.
constexpr float kSilence = 1e-4f;

// The half-band low-pass that halves the rate: a sinc at the output Nyquist under a Blackman window, unit
// DC gain (fosfora's decimate.rs).
const std::array<float, kTaps>& halfbandTaps() {
    static const std::array<float, kTaps> taps = [] {
        std::array<double, kTaps> h{};
        const double mid = (kTaps - 1) / 2.0;
        double sum = 0.0;
        for (int i = 0; i < kTaps; i++) {
            const double x = i - mid;
            const double sinc = x == 0.0 ? 0.5 : std::sin(0.5 * std::numbers::pi * x) / (std::numbers::pi * x);
            const double n = static_cast<double>(i) / (kTaps - 1);
            const double blackman = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * n) + 0.08 * std::cos(4.0 * std::numbers::pi * n);
            h[i] = sinc * blackman;
            sum += h[i];
        }
        std::array<float, kTaps> out{};
        for (int i = 0; i < kTaps; i++) out[i] = static_cast<float>(h[i] / sum);
        return out;
    }();
    return taps;
}

// Rust's f64::round_ties_even, the bin a band edge falls in.
int bin(double hz, double binHz) { return static_cast<int>(std::nearbyint(hz / binHz)); }

// fosfora's Hann: symmetric, in f32, its magnitudes scaled by 2/Σw so a full-scale sine reads 1.
float hann(std::vector<float>& w) {
    const int n = static_cast<int>(w.size());
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        w[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(n - 1)));
        sum += w[i];
    }
    return 2.0f / sum;
}

}  // namespace

float KickModel::score(const std::array<float, kInputs>& x) {
    float raw = kKickBaseline;
    for (uint16_t root : kRoots) {
        int i = root;
        while (kNodes[i].feature != 255) i = x[kNodes[i].feature] <= kNodes[i].v ? kNodes[i].left : kNodes[i].right;
        raw += kNodes[i].v;
    }
    return 1.0f / (1.0f + std::exp(-raw));
}

float KickModel::kickFromProbability(float p) {
    const float t = kKickThreshold;
    return std::clamp(p < t ? 0.5f * p / t : 0.5f + 0.5f * (p - t) / (1.0f - t), 0.0f, 1.0f);
}

void KickModel::configure(int sampleRateHz) {
    int factor = 1;
    stages_ = 0;
    while (sampleRateHz / (factor * 2) >= kMinAnalysisRate && stages_ < static_cast<int>(halfbands_.size())) {
        factor *= 2;
        stages_++;
    }
    analysisRate_ = std::max(1, sampleRateHz / factor);
    const double sr = analysisRate_;
    const double binL = sr / kLarge;
    const double binM = sr / kMedium;
    for (int i = 0; i < kBands; i++) {
        const bool large = kEdges[i + 1] <= kLargeMaxHz;
        const double binHz = large ? binL : binM;
        const int lo = bin(kEdges[i], binHz);
        bands_[i] = Band{large, std::max(lo, 1), std::max(bin(kEdges[i + 1], binHz), lo + 1)};
    }
    lowLo_ = bin(kLowSpanLo, binL);
    lowHi_ = bin(kLowSpanHi, binL);
    dcR_ = std::exp(-2.0f * std::numbers::pi_v<float> * static_cast<float>(kDcCutoffHz) / static_cast<float>(analysisRate_));
    ring_.assign(kLarge, 0.0f);
    frame_.assign(kLarge, 0.0f);
    windowed_.assign(kLarge, 0.0f);
    largeWin_.resize(kLarge);
    mediumWin_.resize(kMedium);
    largeScale_ = hann(largeWin_);
    mediumScale_ = hann(mediumWin_);
    largeMag_.assign(kLarge / 2 + 1, 0.0f);
    largePrev_.assign(kLarge / 2 + 1, 0.0f);
    mediumMag_.assign(kMedium / 2 + 1, 0.0f);
    mediumPrev_.assign(kMedium / 2 + 1, 0.0f);
    (void)halfbandTaps();
    reset();
}

void KickModel::reset() {
    for (Halfband& h : halfbands_) h = Halfband{};
    dcX_ = dcY_ = 0.0f;
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    std::fill(largeMag_.begin(), largeMag_.end(), 0.0f);
    std::fill(largePrev_.begin(), largePrev_.end(), 0.0f);
    std::fill(mediumMag_.begin(), mediumMag_.end(), 0.0f);
    std::fill(mediumPrev_.begin(), mediumPrev_.end(), 0.0f);
    history_ = {};
    head_ = 0;
    fill_ = 0;
    analysed_ = 0;
    probability_ = 0.0f;
    kick_ = 0.0f;
    armed_ = false;
    lastKick_ = -1e9;
}

bool KickModel::push(float mono) {
    // fosfora decimates the stereo pair and then mixes it; the filter is linear, so halving the mix is the
    // same. The first input of each pair is kept.
    float x = mono;
    const std::array<float, kTaps>& taps = halfbandTaps();
    for (int s = 0; s < stages_; s++) {
        Halfband& h = halfbands_[s];
        h.hist[h.head] = x;
        h.head = (h.head + 1) % kTaps;
        const bool emit = h.emit;
        h.emit = !h.emit;
        if (!emit) return false;
        float y = 0.0f;
        for (int t = 0; t < kTaps; t++) y += taps[t] * h.hist[(h.head + t) % kTaps];  // oldest first
        x = y;
    }
    return analyse(x);
}

bool KickModel::analyse(float x) {
    // fosfora strips DC before any stage sees the hop: y = x − x₋₁ + r·y₋₁.
    const float y = x - dcX_ + dcR_ * dcY_;
    dcX_ = x;
    dcY_ = y;
    ring_[head_] = y;
    head_ = (head_ + 1) % kLarge;
    analysed_++;
    if (++fill_ < kHop) return false;
    fill_ = 0;
    fired_ = false;
    hop();
    return fired_;
}

void KickModel::hop() {
    for (int i = 0; i < kLarge; i++) frame_[i] = ring_[(head_ + i) % kLarge];  // oldest first

    // The two spectra over the newest samples, each hop's magnitudes beside the last hop's.
    std::swap(largeMag_, largePrev_);
    std::swap(mediumMag_, mediumPrev_);
    const std::vector<float>& win = frame_;
    for (int i = 0; i < kLarge; i++) windowed_[i] = win[i] * largeWin_[i];
    large_.magnitudes(windowed_.data(), largeMag_.data());
    for (float& m : largeMag_) m *= largeScale_;
    for (int i = 0; i < kMedium; i++) windowed_[i] = win[kLarge - kMedium + i] * mediumWin_[i];
    medium_.magnitudes(windowed_.data(), mediumMag_.data());
    for (float& m : mediumMag_) m *= mediumScale_;

    std::array<float, kFrame> f{};
    features(f);
    std::array<float, kInputs> in{};
    std::copy(f.begin(), f.end(), in.begin());
    for (int k = 0; k < kContext - 1; k++) std::copy(history_[k].begin(), history_[k].end(), in.begin() + kFrame * (k + 1));
    for (int k = kContext - 2; k > 0; k--) history_[k] = history_[k - 1];
    history_[0] = f;
    probability_ = score(in);

    float squares = 0.0f;
    for (int i = kLarge - 2048; i < kLarge; i++) squares += win[i] * win[i];
    const bool silent = std::sqrt(squares / 2048.0f) < kSilence;
    kick_ = silent ? 0.0f : kickFromProbability(probability_);

    const double now = seconds();
    if (kick_ < kRearm) armed_ = true;
    if (armed_ && kick_ >= kFire && now - lastKick_ >= kMinGap) {
        armed_ = false;
        lastKick_ = now;
        fired_ = true;
    }
}

void KickModel::features(std::array<float, kFrame>& f) const {
    std::array<double, kBands> levels{};
    for (int i = 0; i < kBands; i++) {
        const Band& b = bands_[i];
        const std::vector<float>& cur = b.large ? largeMag_ : mediumMag_;
        const std::vector<float>& prev = b.large ? largePrev_ : mediumPrev_;
        const int len = static_cast<int>(cur.size());
        const int lo = std::min(b.lo, len);
        const int hi = std::min(b.hi, len);
        const double n = std::max(hi - lo, 1);
        double flux = 0.0;
        double power = 0.0;
        for (int k = lo; k < hi; k++) {
            const double c = cur[k];
            const double d = std::log(std::max(c, kFluxFloor)) - std::log(std::max(static_cast<double>(prev[k]), kFluxFloor));
            if (d > 0.0) flux += d;
            power += c * c;
        }
        f[i] = static_cast<float>(flux / n);
        levels[i] = std::log(std::max(power / n, 1e-12));
    }
    double mean = 0.0;
    for (double l : levels) mean += l;
    mean /= kBands;
    for (int i = 0; i < kBands; i++) f[kBands + i] = static_cast<float>(levels[i] - mean);

    const int len = static_cast<int>(largeMag_.size());
    const int lo = std::min(lowLo_, len);
    const int hi = std::min(lowHi_, len);
    const double n = std::max(hi - lo, 1);
    double sum = 0.0;
    double logSum = 0.0;
    double peak = 0.0;
    for (int k = lo; k < hi; k++) {
        const double m = largeMag_[k];
        sum += m;
        logSum += std::log(std::max(m, 1e-9));
        peak = std::max(peak, m);
    }
    const double meanMag = std::max(sum / n, 1e-12);
    f[2 * kBands] = static_cast<float>(std::exp(logSum / n) / meanMag);
    f[2 * kBands + 1] = static_cast<float>(peak / meanMag);
}

}  // namespace geode::sound
