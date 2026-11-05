#include "sound/offline/OfflineForm.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

#include "sound/Fft.hpp"

// Every threshold is in bars or decibels, never in seconds or frames, so a song means the same at any tempo
// and any sample rate. core/tests/offline_form_test.cpp holds the boundaries, the drop, the states and the
// returning section on songs built in code; analysis_bench scores the boundaries and the drop on the lab's
// clips beside librosa's oracle-k reference.

namespace geode::sound::offline {

namespace {

constexpr double kTau = 2.0 * std::numbers::pi;
constexpr int kChroma = 12;
constexpr int kMfcc = 13;
constexpr int kDims = kChroma + kMfcc;

// Boundaries.
constexpr int kKernelBars = 4;          // the checkerboard's half: four bars before, four after
constexpr int kPeakBars = 2;            // a peak is the highest within two bars either side ...
constexpr int kMinSectionBars = 4;      // ... and sections are at least four bars (a build often is exactly)
constexpr double kNoveltyFloor = 0.25;  // novelty a boundary must reach, over the typical beat distance ...
constexpr double kOfStrongest = 0.3;    // ... and this share of the track's strongest boundary
constexpr double kSnap = 0.9;           // a peak moves to its nearest downbeat if the novelty there is this close

// Drops (Yadati et al., ISMIR 2014: the bass coming back after a build is the cue, as live Form reads it).
constexpr int kDropWindowBars = 2;  // the low band is compared over two bars either side
constexpr double kDropJumpDb = 6.0;
constexpr double kDropBassOut = 0.5;  // the bass share before is at most this of the share after
constexpr double kDropNearPeakDb = 4.0;  // and what follows is near the track's loud end
constexpr int kRiseBars = 4;             // the build: four bars before rising in one of ...
constexpr double kRiseLoudDb = 0.5;      // ... loudness (dB a bar),
constexpr double kRiseCentroid = 0.03;   // ... brightness (share a bar),
constexpr double kRiseOnsets = 0.08;     // ... or onsets (share a bar)
constexpr int kBuildBars = 8;            // buildUp ramps over at most this before a drop
constexpr int kDropApartBars = 8;
constexpr double kLowHz = 150.0;

// Sections and states.
constexpr double kSameSection = 0.35;  // a returning section sits this close, against how far apart sections are
constexpr int kMaxSections = 8;        // sectionId's range, as live's
constexpr double kCountWrap = 4096.0;

double frac(double x) { return x - std::floor(x); }

// Per analysis frame (OfflineBeats' resolution: librosa's hop and window scaled to the rate).
struct Frames {
    int count = 0;
    double rate = 0.0;            // frames a second
    std::vector<float> chroma;    // count × 12, the loudest class 1 (librosa's norm=inf)
    std::vector<float> mfcc;      // count × 13
    std::vector<double> power;    // total power
    std::vector<double> low;      // power below kLowHz
    std::vector<double> centroid; // Hz
    std::vector<float> onset;     // librosa onset strength (median over the mel bands)
};

Frames frameFeatures(const float* mono, size_t samples, int sampleRateHz) {
    const Params params = paramsFor(sampleRateHz);
    const MelSpectrogram mel = melSpectrogram(mono, samples, sampleRateHz, params);
    Frames fr;
    fr.count = mel.frames;
    fr.rate = static_cast<double>(sampleRateHz) / params.hop;
    fr.onset = onsetStrength(mel, params, Aggregate::Median);

    // MFCC: librosa.feature.mfcc's DCT-II with norm="ortho" over the same dB mel spectrogram.
    fr.mfcc.assign(static_cast<size_t>(fr.count) * kMfcc, 0.0f);
    std::vector<double> basis(static_cast<size_t>(kMfcc) * mel.mels);
    for (int k = 0; k < kMfcc; ++k)
        for (int m = 0; m < mel.mels; ++m)
            basis[static_cast<size_t>(k) * mel.mels + m] =
                std::cos(std::numbers::pi * k * (2.0 * m + 1.0) / (2.0 * mel.mels)) *
                std::sqrt((k == 0 ? 1.0 : 2.0) / mel.mels);
    for (int t = 0; t < fr.count; ++t)
        for (int k = 0; k < kMfcc; ++k) {
            double s = 0.0;
            for (int m = 0; m < mel.mels; ++m) s += basis[static_cast<size_t>(k) * mel.mels + m] * mel.at(t, m);
            fr.mfcc[static_cast<size_t>(t) * kMfcc + k] = static_cast<float>(s);
        }

    // The rest from one STFT framed exactly as the mel spectrogram (centred, periodic Hann), so frame t is
    // the same instant in both. Chroma folds each bin from 55 Hz to 4.2 kHz to its nearest pitch class.
    const int n = params.fftSize, hop = params.hop, bins = n / 2 + 1;
    std::vector<int> pitchClass(bins, -1);
    std::vector<char> isLow(bins, 0);
    for (int k = 1; k < bins; ++k) {
        const double hz = static_cast<double>(k) * sampleRateHz / n;
        if (hz >= 55.0 && hz <= 4200.0) {
            const long midi = std::lround(69.0 + 12.0 * std::log2(hz / 440.0));
            pitchClass[k] = static_cast<int>(((midi % 12) + 12) % 12);
        }
        isLow[k] = hz >= 20.0 && hz < kLowHz;
    }
    std::vector<float> window(n);
    for (int i = 0; i < n; ++i) window[i] = static_cast<float>(0.5 - 0.5 * std::cos(kTau * i / n));
    Fft fft(n);
    std::vector<float> frame(n), mag(bins);
    fr.chroma.assign(static_cast<size_t>(fr.count) * kChroma, 0.0f);
    fr.power.assign(fr.count, 0.0);
    fr.low.assign(fr.count, 0.0);
    fr.centroid.assign(fr.count, 0.0);
    for (int t = 0; t < fr.count; ++t) {
        const int64_t start = static_cast<int64_t>(t) * hop - n / 2;
        for (int i = 0; i < n; ++i) {
            const int64_t at = start + i;
            frame[i] = at >= 0 && at < static_cast<int64_t>(samples) ? mono[at] * window[i] : 0.0f;
        }
        fft.magnitudes(frame.data(), mag.data());
        double chroma[kChroma] = {};
        double power = 0.0, low = 0.0, weighted = 0.0, magSum = 0.0;
        for (int k = 1; k < bins; ++k) {
            const double p = static_cast<double>(mag[k]) * mag[k];
            power += p;
            if (isLow[k]) low += p;
            if (pitchClass[k] >= 0) chroma[pitchClass[k]] += p;
            weighted += static_cast<double>(k) * sampleRateHz / n * mag[k];
            magSum += mag[k];
        }
        const double peak = *std::max_element(chroma, chroma + kChroma);
        for (int c = 0; c < kChroma; ++c)
            fr.chroma[static_cast<size_t>(t) * kChroma + c] = peak > 0.0 ? static_cast<float>(chroma[c] / peak) : 0.0f;
        fr.power[t] = power;
        fr.low[t] = low;
        fr.centroid[t] = magSum > 0.0 ? weighted / magSum : 0.0;
    }
    return fr;
}

// The beats the features are taken on: the grid, carried on at its tempo to both ends of the track so the
// intro's lead-in and the outro's ring-out have beats too (the kernel needs four bars either side of a
// boundary, and the last section is often exactly four). Too short a grid becomes a 120 BPM pulse from 0.
struct Pulse {
    std::vector<double> beats;  // seconds, ascending
    int beatsPerBar = 4;
    int downbeatPhase = 0;  // beats[downbeatPhase + k · beatsPerBar] are downbeats
    bool hasDownbeats = false;
};

Pulse pulseFor(const BeatGrid& grid, double duration) {
    Pulse p;
    p.beatsPerBar = std::max(1, grid.beatsPerBar);
    if (grid.beats.size() < static_cast<size_t>(2 * p.beatsPerBar)) {
        for (double t = 0.0; t < duration; t += 0.5) p.beats.push_back(t);
        return p;
    }
    const auto& b = grid.beats;
    const double period = grid.bpm > 0.0 ? 60.0 / grid.bpm : (b.back() - b.front()) / static_cast<double>(b.size() - 1);
    std::vector<double> before;
    for (double t = b.front() - period; t > 0.0; t -= period) before.push_back(t);
    std::reverse(before.begin(), before.end());
    p.beats = before;
    p.beats.insert(p.beats.end(), b.begin(), b.end());
    for (double t = b.back() + period; t < duration; t += period) p.beats.push_back(t);
    const int pre = static_cast<int>(before.size());
    p.downbeatPhase = (grid.downbeatPhase + pre) % p.beatsPerBar;
    p.hasDownbeats = true;
    return p;
}

// Per beat of the pulse: beat i spans [beats[i], beats[i+1]), the last one to the end of the track.
struct Beats {
    int count = 0;
    std::vector<double> z;  // count × kDims: median chroma and mean MFCC, z-scored per dimension
    std::vector<double> power, low, centroid, onset;
};

Beats beatFeatures(const Frames& fr, const Pulse& pulse, double duration) {
    Beats bt;
    bt.count = static_cast<int>(pulse.beats.size());
    bt.z.assign(static_cast<size_t>(bt.count) * kDims, 0.0);
    bt.power.assign(bt.count, 0.0);
    bt.low.assign(bt.count, 0.0);
    bt.centroid.assign(bt.count, 0.0);
    bt.onset.assign(bt.count, 0.0);
    auto frameAt = [&](double t) { return std::clamp(static_cast<int>(std::lround(t * fr.rate)), 0, fr.count); };
    std::vector<float> column;
    for (int i = 0; i < bt.count; ++i) {
        const int f0 = std::min(frameAt(pulse.beats[i]), fr.count - 1);
        const int f1 = std::max(f0 + 1, frameAt(i + 1 < bt.count ? pulse.beats[i + 1] : duration));
        const int len = std::min(f1, fr.count) - f0;
        double* z = bt.z.data() + static_cast<size_t>(i) * kDims;
        for (int c = 0; c < kChroma; ++c) {  // librosa.util.sync(chroma, aggregate=np.median), as the lab
            column.clear();
            for (int f = f0; f < f0 + len; ++f) column.push_back(fr.chroma[static_cast<size_t>(f) * kChroma + c]);
            const size_t m = column.size();
            std::nth_element(column.begin(), column.begin() + m / 2, column.end());
            double med = column[m / 2];
            if (m % 2 == 0) med = 0.5 * (med + *std::max_element(column.begin(), column.begin() + m / 2));
            z[c] = med;
        }
        for (int f = f0; f < f0 + len; ++f) {
            for (int k = 0; k < kMfcc; ++k) z[kChroma + k] += fr.mfcc[static_cast<size_t>(f) * kMfcc + k];
            bt.power[i] += fr.power[f];
            bt.low[i] += fr.low[f];
            bt.centroid[i] += fr.centroid[f];
            bt.onset[i] += fr.onset[f];
        }
        for (int k = 0; k < kMfcc; ++k) z[kChroma + k] /= len;
        bt.power[i] /= len;
        bt.low[i] /= len;
        bt.centroid[i] /= len;
        bt.onset[i] /= len;
    }
    // Each dimension over the track to zero mean and unit spread, as the lab does before clustering, so
    // chroma and timbre have an equal say whatever their units.
    for (int d = 0; d < kDims; ++d) {
        double mean = 0.0, var = 0.0;
        for (int i = 0; i < bt.count; ++i) mean += bt.z[static_cast<size_t>(i) * kDims + d];
        mean /= std::max(1, bt.count);
        for (int i = 0; i < bt.count; ++i) {
            const double v = bt.z[static_cast<size_t>(i) * kDims + d] - mean;
            var += v * v;
        }
        const double sd = std::sqrt(var / std::max(1, bt.count)) + 1e-9;
        for (int i = 0; i < bt.count; ++i) {
            double& v = bt.z[static_cast<size_t>(i) * kDims + d];
            v = (v - mean) / sd;
        }
    }
    return bt;
}

double distance(const double* a, const double* b) {
    double s = 0.0;
    for (int d = 0; d < kDims; ++d) s += (a[d] - b[d]) * (a[d] - b[d]);
    return s / kDims;
}

// Foote's checkerboard with a box kernel, `half` beats each side, at every beat: how much further apart
// the two sides are than each side is within itself, over the track's typical distance between beats a few
// bars apart. 0 where the kernel does not fit.
std::vector<double> noveltyCurve(const Beats& bt, int half) {
    const int n = bt.count, span = 2 * half;
    std::vector<double> nov(n, 0.0);
    if (n < span + 1) return nov;
    // The band of distances the kernel reads: band[i·span + d] = distance(i, i + d), d < span.
    std::vector<double> band(static_cast<size_t>(n) * span, 0.0);
    double typical = 0.0;
    int pairs = 0;
    for (int i = 0; i < n; ++i)
        for (int d = 1; d < span && i + d < n; ++d) {
            const double v = distance(&bt.z[static_cast<size_t>(i) * kDims], &bt.z[static_cast<size_t>(i + d) * kDims]);
            band[static_cast<size_t>(i) * span + d] = v;
            typical += v;
            ++pairs;
        }
    typical = pairs ? typical / pairs : 0.0;
    if (typical < 1e-9) return nov;
    auto dist = [&](int a, int b) {
        if (a > b) std::swap(a, b);
        return band[static_cast<size_t>(a) * span + (b - a)];
    };
    const double l2 = static_cast<double>(half) * half;
    for (int i = half; i + half <= n; ++i) {
        double within = 0.0, cross = 0.0;
        for (int u = i - half; u < i; ++u) {
            for (int v = i - half; v < i; ++v) within += dist(u, v);
            for (int v = i; v < i + half; ++v) cross += dist(u, v);
        }
        for (int u = i; u < i + half; ++u)
            for (int v = i; v < i + half; ++v) within += dist(u, v);
        nov[i] = std::max(0.0, (cross / l2 - within / (2.0 * l2)) / typical);
    }
    return nov;
}

double toDb(double power, double floor) { return 10.0 * std::log10(power + floor); }

// The least-squares slope of y[from..to) per beat.
double slope(const std::vector<double>& y, int from, int to) {
    const int n = to - from;
    if (n < 2) return 0.0;
    const double mx = 0.5 * (n - 1);
    double my = 0.0;
    for (int i = from; i < to; ++i) my += y[i];
    my /= n;
    double num = 0.0, den = 0.0;
    for (int i = from; i < to; ++i) {
        const double dx = (i - from) - mx;
        num += dx * (y[i] - my);
        den += dx * dx;
    }
    return num / den;
}

struct DropCandidate {
    bool valid = false;
    double jump = 0.0;  // dB
};

// Whether beat c is a drop: two bars on either side, the low band up kDropJumpDb, its share of the mix
// at least doubled (the bass was pulled out, not just quieter), the bars after it near the track's loud
// end, and the four bars before it rising in loudness, brightness or onsets.
std::vector<DropCandidate> dropCandidates(const Beats& bt, int bpb, double floor, double loudTop) {
    const int n = bt.count, w = kDropWindowBars * bpb, rise = kRiseBars * bpb;
    std::vector<DropCandidate> out(n);
    std::vector<double> loudDb(n);
    for (int i = 0; i < n; ++i) loudDb[i] = toDb(bt.power[i], floor);
    for (int c = std::max(w, rise); c + w <= n; ++c) {
        double lowB = 0.0, lowA = 0.0, powB = 0.0, powA = 0.0;
        for (int i = c - w; i < c; ++i) lowB += bt.low[i], powB += bt.power[i];
        for (int i = c; i < c + w; ++i) lowA += bt.low[i], powA += bt.power[i];
        const double jump = toDb(lowA / w, floor) - toDb(lowB / w, floor);
        const double shareB = powB > 0.0 ? lowB / powB : 0.0, shareA = powA > 0.0 ? lowA / powA : 0.0;
        if (jump < kDropJumpDb || shareB > kDropBassOut * shareA || toDb(powA / w, floor) < loudTop - kDropNearPeakDb) continue;
        double centroidMean = 0.0, onsetMean = 0.0;
        for (int i = c - rise; i < c; ++i) centroidMean += bt.centroid[i], onsetMean += bt.onset[i];
        centroidMean /= rise;
        onsetMean /= rise;
        const bool louder = slope(loudDb, c - rise, c) * bpb >= kRiseLoudDb;
        const bool brighter = centroidMean > 0.0 && slope(bt.centroid, c - rise, c) * bpb / centroidMean >= kRiseCentroid;
        const bool busier = onsetMean > 0.0 && slope(bt.onset, c - rise, c) * bpb / onsetMean >= kRiseOnsets;
        if (!(louder || brighter || busier)) continue;
        out[c] = {true, jump};
    }
    return out;
}

// The downbeat nearest beat i (within half a bar), or -1.
int nearestDownbeat(const Pulse& p, int i, int count) {
    if (!p.hasDownbeats) return -1;
    const int bpb = p.beatsPerBar;
    const int back = ((i - p.downbeatPhase) % bpb + bpb) % bpb;
    const int lo = i - back, hi = lo + bpb;
    const int pick = back <= bpb - back ? lo : hi;
    return pick >= 0 && pick < count ? pick : (lo >= 0 ? lo : -1);
}

// Strongest first, keeping those at least `gap` beats from every one kept; returns them ascending.
std::vector<int> spaced(std::vector<int> candidates, const std::vector<double>& score, int gap) {
    std::stable_sort(candidates.begin(), candidates.end(), [&](int a, int b) { return score[a] > score[b]; });
    std::vector<int> kept;
    for (int c : candidates)
        if (std::none_of(kept.begin(), kept.end(), [&](int k) { return std::abs(k - c) < gap; })) kept.push_back(c);
    std::sort(kept.begin(), kept.end());
    return kept;
}

}  // namespace

FormTimeline analyseForm(const float* mono, size_t samples, int sampleRateHz, const BeatGrid& grid) {
    FormTimeline form;
    if (!mono || samples == 0 || sampleRateHz <= 0) return form;
    form.duration = static_cast<double>(samples) / sampleRateHz;
    const Frames fr = frameFeatures(mono, samples, sampleRateHz);
    const double peakPower = fr.count ? *std::max_element(fr.power.begin(), fr.power.end()) : 0.0;
    if (peakPower <= 1e-12) return form;  // silence: no sections
    const double floor = 1e-9 * peakPower;  // -90 dB under the loudest frame, so silence is not -inf

    const Pulse pulse = pulseFor(grid, form.duration);
    const Beats bt = beatFeatures(fr, pulse, form.duration);
    const int n = bt.count, bpb = pulse.beatsPerBar;
    if (n == 0) return form;

    // Novelty and its peaks.
    const std::vector<double> nov = noveltyCurve(bt, kKernelBars * bpb);
    form.noveltyTimes = pulse.beats;
    form.novelty.resize(n);
    for (int i = 0; i < n; ++i) form.novelty[i] = static_cast<float>(std::min(1.0, nov[i]));
    const double strongest = *std::max_element(nov.begin(), nov.end());
    const double threshold = std::max(kNoveltyFloor, kOfStrongest * strongest);
    std::vector<int> peaks;
    const int around = kPeakBars * bpb;
    for (int i = 0; i < n; ++i) {
        if (nov[i] < threshold) continue;
        bool top = true;
        for (int j = std::max(0, i - around); j <= std::min(n - 1, i + around) && top; ++j)
            if (j != i && (j < i ? nov[j] >= nov[i] : nov[j] > nov[i])) top = false;
        if (top) peaks.push_back(i);
    }
    // Sections start on bar lines; a peak moves to its nearest downbeat unless that is clearly worse (the
    // box kernel's peak is a ridge one beat either side, so an off-bar peak is usually a beat of noise).
    for (int& p : peaks) {
        const int d = nearestDownbeat(pulse, p, n);
        if (d >= 0 && nov[d] >= kSnap * nov[p]) p = d;
    }
    std::sort(peaks.begin(), peaks.end());
    peaks.erase(std::unique(peaks.begin(), peaks.end()), peaks.end());
    peaks = spaced(peaks, nov, kMinSectionBars * bpb);

    // Drops, then each one is also a boundary: it replaces a novelty boundary within a bar of it (the drop
    // is the sharper cue) or joins as a new one.
    std::vector<double> loudSorted(n);
    for (int i = 0; i < n; ++i) loudSorted[i] = toDb(bt.power[i], floor);
    std::sort(loudSorted.begin(), loudSorted.end());
    const double loudTop = loudSorted[static_cast<size_t>(0.9 * (n - 1))];
    const std::vector<DropCandidate> cand = dropCandidates(bt, bpb, floor, loudTop);
    std::vector<double> jumps(n, 0.0);
    std::vector<int> dropBeats;
    for (int i = 0; i < n; ++i)
        if (cand[i].valid) {
            jumps[i] = cand[i].jump;
            dropBeats.push_back(i);
        }
    dropBeats = spaced(dropBeats, jumps, kDropApartBars * bpb);
    // A drop moves to its downbeat only on a near tie: the jump falls steeply a beat late (one beat of bass
    // in the "before" window) but only ~0.6 dB a beat early, so a wider margin would pull a true drop onto
    // the beat before it whenever the downbeat phase is off.
    for (int& d : dropBeats) {
        const int db = nearestDownbeat(pulse, d, n);
        if (db >= 0 && db != d && cand[db].valid && cand[db].jump >= cand[d].jump - 0.25) d = db;
    }
    std::vector<char> isDrop(n, 0);
    for (int d : dropBeats) {
        isDrop[d] = 1;
        peaks.erase(std::remove_if(peaks.begin(), peaks.end(), [&](int p) { return std::abs(p - d) <= bpb; }), peaks.end());
        peaks.push_back(d);
    }
    std::sort(peaks.begin(), peaks.end());
    // Keep sections at least kMinSectionBars apart; a drop wins over a novelty boundary, else the stronger.
    std::vector<int> bounds;
    for (int p : peaks) {
        if (!bounds.empty() && p - bounds.back() < kMinSectionBars * bpb) {
            const int q = bounds.back();
            const bool keepNew = isDrop[p] && !isDrop[q] ? true : (isDrop[q] && !isDrop[p] ? false : nov[p] > nov[q]);
            if (keepNew) bounds.back() = p;
            continue;
        }
        bounds.push_back(p);
    }

    // Sections, each with its strength, mean features, loudness and bass share.
    std::vector<int> startBeat{0};
    for (int b : bounds)
        if (b > 0) startBeat.push_back(b);
    const int m = static_cast<int>(startBeat.size());
    std::vector<std::vector<double>> mean(m, std::vector<double>(kDims, 0.0));
    std::vector<double> loud(m), share(m);
    double trackLow = 0.0, trackPower = 0.0;
    for (int i = 0; i < n; ++i) trackLow += bt.low[i], trackPower += bt.power[i];
    const double trackShare = trackPower > 0.0 ? trackLow / trackPower : 0.0;
    for (int s = 0; s < m; ++s) {
        const int from = startBeat[s], to = s + 1 < m ? startBeat[s + 1] : n;
        double pw = 0.0, lw = 0.0;
        for (int i = from; i < to; ++i) {
            for (int d = 0; d < kDims; ++d) mean[s][d] += bt.z[static_cast<size_t>(i) * kDims + d];
            pw += bt.power[i];
            lw += bt.low[i];
        }
        for (double& v : mean[s]) v /= std::max(1, to - from);
        loud[s] = toDb(pw / std::max(1, to - from), floor);
        share[s] = pw > 0.0 && trackShare > 0.0 ? (lw / pw) / trackShare : 1.0;
        FormSection sec;
        sec.start = s == 0 ? 0.0 : pulse.beats[from];
        if (s > 0) {
            const float strength = isDrop[from] ? 0.0f : static_cast<float>(std::clamp(nov[from], 0.0, 1.0));
            sec.strength = std::max(kFormEventFloor, strength);
        }
        form.sections.push_back(sec);
    }

    // Drops, with their builds: from the start of the section the drop ends, at most kBuildBars back and
    // never before the last drop.
    int lastDrop = 0;
    for (int d : dropBeats) {
        FormDrop drop;
        drop.at = pulse.beats[d];
        int section = 0;
        while (section + 1 < m && startBeat[section + 1] < d) ++section;
        const int buildBeat = std::max({startBeat[section], d - kBuildBars * bpb, lastDrop});
        drop.buildStart = pulse.beats[buildBeat];
        drop.strength = static_cast<float>(std::clamp(kFormEventFloor + 0.6 * (cand[d].jump - kDropJumpDb) / 12.0,
                                                      static_cast<double>(kFormEventFloor), 1.0));
        form.drops.push_back(drop);
        lastDrop = d;
        for (int s = 1; s < m; ++s)
            if (startBeat[s] == d) form.sections[s].strength = drop.strength;
    }

    // sectionId: a section like an earlier one (closer than kSameSection of the median distance between
    // sections) takes its number back; a new one takes the next free number, or the longest unused.
    std::vector<double> apart;
    for (int a = 0; a < m; ++a)
        for (int b = a + 1; b < m; ++b) apart.push_back(distance(mean[a].data(), mean[b].data()));
    double medianApart = 0.0;
    if (!apart.empty()) {
        std::nth_element(apart.begin(), apart.begin() + apart.size() / 2, apart.end());
        medianApart = apart[apart.size() / 2];
    }
    std::vector<int> lastUsed(kMaxSections, -1);
    int ids = 0;
    for (int s = 0; s < m; ++s) {
        int best = -1;
        double bestD = std::numeric_limits<double>::infinity();
        for (int r = 0; r < s; ++r) {
            const double d = distance(mean[s].data(), mean[r].data());
            if (d < bestD) bestD = d, best = r;
        }
        int id;
        if (best >= 0 && m >= 3 && bestD < kSameSection * medianApart) {
            id = form.sections[best].id;
        } else if (ids < kMaxSections) {
            id = ids++;
        } else {
            id = static_cast<int>(std::min_element(lastUsed.begin(), lastUsed.end()) - lastUsed.begin());
        }
        form.sections[s].id = id;
        lastUsed[id] = s;
    }

    // States: live Form's rules (classifyAtSectionStart, Form.cpp), seeing the whole song. The peak needs
    // a contrast to stand out from: a steady loop is a verse throughout, where live would call it a drop.
    const double loudRef = *std::max_element(loud.begin(), loud.end());
    const double loudLow = *std::min_element(loud.begin(), loud.end());
    const bool contrast = m >= 2 && (loudRef - loudLow >= 3.0 || *std::min_element(share.begin(), share.end()) < 0.5);
    for (int s = 0; s < m; ++s) {
        const double rel = loud[s] - loudRef;
        int state = GEODE_SECTION_VERSE;
        if (isDrop[startBeat[s]] && s > 0) state = GEODE_SECTION_DROP;
        else if (s == 0 && m >= 2 && rel <= -1.5) state = GEODE_SECTION_INTRO;
        else if (s == m - 1 && m >= 2 && rel <= -3.0) state = GEODE_SECTION_OUTRO;
        else if (contrast && rel >= -2.5 && share[s] >= 0.8) state = GEODE_SECTION_DROP;
        else if (contrast && (rel <= -6.0 || share[s] < 0.4)) state = GEODE_SECTION_BREAKDOWN;
        form.states.push_back({form.sections[s].start, state});
    }
    // Each build overlays its stretch before the drop.
    for (const FormDrop& drop : form.drops) {
        auto& st = form.states;
        st.erase(std::remove_if(st.begin(), st.end(), [&](const FormState& x) { return x.start >= drop.buildStart && x.start < drop.at; }),
                 st.end());
        const bool fromStart = drop.buildStart <= 0.0;
        st.push_back({fromStart ? 0.0 : drop.buildStart, GEODE_SECTION_BUILD});
        std::stable_sort(st.begin(), st.end(), [](const FormState& a, const FormState& b) { return a.start < b.start; });
    }
    std::vector<FormState> merged;
    for (const FormState& st : form.states)
        if (merged.empty() || merged.back().state != st.state) merged.push_back(st);
    form.states = merged;

    for (size_t s = 1; s < form.sections.size(); ++s) form.boundaries.push_back(form.sections[s].start);
    return form;
}

namespace {

// Bars from the grid's first downbeat, fractional: the downbeats inside the grid, its tempo outside.
double barPosition(const BeatGrid& g, double t) {
    const auto& d = g.downbeats;
    const int bpb = std::max(1, g.beatsPerBar);
    const double barPeriod = g.bpm > 0.0 ? bpb * 60.0 / g.bpm : 0.0;
    if (d.empty()) return barPeriod > 0.0 ? t / barPeriod : 0.0;
    const size_t k = static_cast<size_t>(std::upper_bound(d.begin(), d.end(), t) - d.begin());
    if (k == 0) return barPeriod > 0.0 ? (t - d.front()) / barPeriod : 0.0;
    if (k >= d.size()) return static_cast<double>(d.size() - 1) + (barPeriod > 0.0 ? (t - d.back()) / barPeriod : 0.0);
    return static_cast<double>(k - 1) + (t - d[k - 1]) / (d[k] - d[k - 1]);
}

int wholeBars(double barPos) { return static_cast<int>(std::floor(barPos + 1e-6)); }

}  // namespace

void sample(const FormTimeline& form, const BeatGrid& grid, double seconds, double dt, GeodeSoundFrame& out) {
    out.sectionChange = out.drop = out.buildUp = 0.0f;
    if (form.sections.empty()) {
        out.novelty = 0.0f;
        out.sectionId = 0.0f;
        out.sectionState = static_cast<float>(GEODE_SECTION_INTRO);
        out.sectionBars = 0.0f;
        return;
    }
    const double from = seconds - dt;
    size_t section = 0;
    for (size_t s = 1; s < form.sections.size(); ++s) {
        const double at = form.sections[s].start;
        if (at <= seconds) section = s;
        if (at > from && at <= seconds) out.sectionChange = std::max(out.sectionChange, form.sections[s].strength);
    }
    out.sectionId = static_cast<float>(form.sections[section].id);

    const FormState* state = &form.states.front();
    for (const FormState& st : form.states)
        if (st.start <= seconds) state = &st;
    out.sectionState = static_cast<float>(state->state);
    const int bars = wholeBars(barPosition(grid, seconds)) - wholeBars(barPosition(grid, state->start));
    out.sectionBars = static_cast<float>(std::fmod(std::max(0, bars), kCountWrap));

    for (const FormDrop& d : form.drops) {
        if (d.at > from && d.at <= seconds) out.drop = std::max(out.drop, d.strength);
        if (seconds >= d.buildStart && seconds < d.at && d.at > d.buildStart)
            out.buildUp = std::max(out.buildUp, static_cast<float>((seconds - d.buildStart) / (d.at - d.buildStart)));
    }
    if (out.drop > 0.0f) out.sectionChange = std::max(out.sectionChange, out.drop);

    const auto& nt = form.noveltyTimes;
    if (!nt.empty()) {
        const size_t k = static_cast<size_t>(std::upper_bound(nt.begin(), nt.end(), seconds) - nt.begin());
        if (k == 0) {
            out.novelty = form.novelty.front();
        } else if (k >= nt.size()) {
            out.novelty = form.novelty.back();
        } else {
            const double w = (seconds - nt[k - 1]) / (nt[k] - nt[k - 1]);
            out.novelty = static_cast<float>((1.0 - w) * form.novelty[k - 1] + w * form.novelty[k]);
        }
    }

    // Phrases restart at each section, as live's do: eight bars from the section's first bar line.
    if (grid.downbeats.empty()) return;
    const double pos = barPosition(grid, seconds);
    if (pos < 0.0) return;  // before the first downbeat the grid's own phrase fields stand
    std::vector<int> startBar(form.sections.size(), 0);
    for (size_t s = 1; s < form.sections.size(); ++s)
        startBar[s] = std::max(startBar[s - 1], static_cast<int>(std::lround(std::max(0.0, barPosition(grid, form.sections[s].start)))));
    const int barNow = wholeBars(pos);
    size_t cur = 0;
    double phrasesBefore = 0.0;
    for (size_t s = 1; s < startBar.size(); ++s) {
        if (startBar[s] > barNow) break;
        phrasesBefore += std::ceil((startBar[s] - startBar[cur]) / 8.0);
        cur = s;
    }
    const double rel = pos - startBar[cur];
    out.phraseIndex = static_cast<float>(std::fmod(phrasesBefore + std::floor(rel / 8.0 + 1e-9), kCountWrap));
    out.phrasePhase = static_cast<float>(frac(rel / 8.0));
    out.phrase = out.downbeat > 0.0f && (barNow - startBar[cur]) % 8 == 0 ? 1.0f : 0.0f;
}

}  // namespace geode::sound::offline
