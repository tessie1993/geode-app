#include "sound/offline/OfflineFeatures.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <numbers>

#include "sound/Fft.hpp"
#include "sound/offline/OfflineBeats.hpp"

namespace geode::sound::offline {

namespace {

constexpr double kTau = 2.0 * std::numbers::pi;
constexpr float kTiny = std::numeric_limits<float>::min();

// A mean over a centred window of ±half frames; at the edges, over what exists. Offline, a level can be
// measured against the music on both sides of it, which is what "relative" means here.
std::vector<double> centredMean(const std::vector<double>& x, int half) {
    const int n = static_cast<int>(x.size());
    std::vector<double> prefix(static_cast<size_t>(n) + 1, 0.0), out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + x[i];
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - half), b = std::min(n, i + half + 1);
        out[i] = (prefix[b] - prefix[a]) / (b - a);
    }
    return out;
}

// The largest value in a centred window of ±half frames (a monotonic deque: linear time).
std::vector<double> centredMax(const std::vector<double>& x, int half) {
    const int n = static_cast<int>(x.size());
    std::vector<double> out(static_cast<size_t>(n));
    std::deque<int> q;
    for (int i = 0; i < n + half; ++i) {
        if (i < n) {
            while (!q.empty() && x[q.back()] <= x[i]) q.pop_back();
            q.push_back(i);
        }
        const int c = i - half;
        if (c < 0) continue;
        while (q.front() < c - half) q.pop_front();
        out[c] = x[q.front()];
    }
    return out;
}

// x over its centred mean, bent smoothly so nothing passes 4 (SoundFrame.h's relative kind). Silence reads 0.
std::vector<float> relative(const std::vector<double>& x, int half) {
    const std::vector<double> mean = centredMean(x, half);
    std::vector<float> out(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        out[i] = mean[i] > 1e-12 ? static_cast<float>(4.0 * std::tanh(x[i] / mean[i] / 4.0)) : 0.0f;
    return out;
}

// SoundFrame.h's log frequency axis: 0 = 20 Hz … 1 = 20 kHz.
float logAxis(double hz) { return hz <= 20.0 ? 0.0f : static_cast<float>(std::clamp(std::log(hz / 20.0) / std::log(1000.0), 0.0, 1.0)); }

float medianOf(float* v, int n) {
    std::nth_element(v, v + n / 2, v + n);
    const float upper = v[n / 2];
    if (n % 2) return upper;
    return 0.5f * (*std::max_element(v, v + n / 2) + upper);
}

// scipy.ndimage's mode="reflect": d c b a | a b c d | d c b a.
int reflect(int i, int n) {
    if (i < 0) return std::min(n - 1, -i - 1);
    if (i >= n) return std::max(0, 2 * n - i - 1);
    return i;
}

// Halves the rate: a 63-tap Blackman-windowed sinc at 0.2 of the input rate, centred, so output sample n
// sits on input sample 2n. The CQT reads at most a fifth of the halved rate, far inside its passband.
std::vector<float> halve(const std::vector<float>& x) {
    constexpr int kHalf = 31;
    static const std::vector<double> taps = [] {
        std::vector<double> h(2 * kHalf + 1);
        double sum = 0.0;
        for (int k = -kHalf; k <= kHalf; ++k) {
            const double sinc = k == 0 ? 0.4 : std::sin(kTau * 0.2 * k) / (std::numbers::pi * k);
            const double w = 0.42 + 0.5 * std::cos(std::numbers::pi * k / (kHalf + 1)) + 0.08 * std::cos(kTau * k / (kHalf + 1));
            h[k + kHalf] = sinc * w;
            sum += h[k + kHalf];
        }
        for (double& v : h) v /= sum;
        return h;
    }();
    const int n = static_cast<int>(x.size());
    std::vector<float> y(static_cast<size_t>((n + 1) / 2));
    for (int i = 0; i < static_cast<int>(y.size()); ++i) {
        double s = 0.0;
        const int c = 2 * i;
        for (int k = std::max(-kHalf, -c); k <= kHalf && c + k < n; ++k) s += taps[k + kHalf] * x[c + k];
        y[i] = static_cast<float>(s);
    }
    return y;
}

// Event times from a non-negative envelope by the onset peak picker.
std::vector<double> pick(const std::vector<float>& envelope, double frameRate) {
    std::vector<double> out;
    for (int f : peakPick(envelope, frameRate)) out.push_back(f / frameRate);
    return out;
}

// Positive change in dB of a band's energy, against the band's loudest frame with a floor 40 dB under it,
// so a near-silent band's noise cannot read as a hit.
std::vector<float> bandOnsets(const std::vector<double>& energy) {
    const double peak = energy.empty() ? 0.0 : *std::max_element(energy.begin(), energy.end());
    std::vector<float> out(energy.size(), 0.0f);
    if (peak <= 0.0) return out;
    double prev = -40.0;
    for (size_t i = 0; i < energy.size(); ++i) {
        const double db = 10.0 * std::log10(std::max(energy[i] / peak, 1e-4));
        out[i] = static_cast<float>(std::max(0.0, db - prev));
        prev = db;
    }
    return out;
}

constexpr double kKk[2][12] = {
    {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88},  // major
    {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17},  // minor
};

// Count of events at or before t.
size_t countUpTo(const std::vector<double>& events, double t) {
    return static_cast<size_t>(std::upper_bound(events.begin(), events.end(), t) - events.begin());
}

}  // namespace

std::vector<int> peakPick(const std::vector<float>& envelope, double frameRate) {
    const int n = static_cast<int>(envelope.size());
    if (n == 0) return {};
    std::vector<float> x(envelope);
    const float lo = *std::min_element(x.begin(), x.end());
    for (float& v : x) v -= lo;
    const float hi = *std::max_element(x.begin(), x.end());
    for (float& v : x) v /= hi + kTiny;
    if (std::none_of(x.begin(), x.end(), [](float v) { return v != 0.0f; })) return {};
    // onset_detect's defaults in frames: (seconds · sr) // hop, then ceil.
    const int preMax = static_cast<int>(std::floor(0.03 * frameRate));
    const int postMax = 1;
    const int preAvg = static_cast<int>(std::floor(0.10 * frameRate));
    const int postAvg = preAvg + 1;
    const int wait = preMax;
    constexpr float kDelta = 0.07f;
    auto maxOf = [&](int a, int b) { return *std::max_element(x.begin() + a, x.begin() + b); };
    auto meanOf = [&](int a, int b) {
        double s = 0.0;
        for (int i = a; i < b; ++i) s += x[i];
        return static_cast<float>(s / (b - a));
    };
    std::vector<int> peaks;
    int i = 1;
    if (x[0] >= maxOf(0, std::min(postMax, n)) && x[0] >= meanOf(0, std::min(postAvg, n)) + kDelta) {
        peaks.push_back(0);
        i = wait + 1;
    }
    while (i < n) {
        if (x[i] != maxOf(std::max(0, i - preMax), std::min(i + postMax, n))) {
            ++i;
            continue;
        }
        if (x[i] < meanOf(std::max(0, i - preAvg), std::min(i + postAvg, n)) + kDelta) {
            ++i;
            continue;
        }
        peaks.push_back(i);
        i += wait + 1;
    }
    return peaks;
}

std::vector<std::array<float, 12>> cqtChroma(const float* mono, size_t samples, int sampleRateHz, int hop, int frames) {
    constexpr int kPerOctave = 36, kOctaves = 7, kBins = kPerOctave * kOctaves;
    constexpr double kFmin = 32.70319566257483;  // C1, tuning 0
    const double r2 = std::pow(2.0, 2.0 / kPerOctave);
    const double q = (r2 + 1.0) / (r2 - 1.0);  // librosa's filter_scale / alpha for equal temperament
    std::vector<std::array<double, 12>> raw(static_cast<size_t>(frames), std::array<double, 12>{});
    std::vector<float> x(mono, mono + samples);
    double rate = sampleRateHz;
    int step = hop;
    std::vector<double> kr, ki;
    for (int o = 0; o < kOctaves; ++o) {
        if (o > 0 && step % 2 == 0) {  // librosa halves while the hop stays whole
            x = halve(x);
            rate /= 2.0;
            step /= 2;
        }
        const int length = static_cast<int>(x.size());
        for (int b = 0; b < kPerOctave; ++b) {
            const int bin = kBins - kPerOctave * (o + 1) + b;
            const double f = kFmin * std::pow(2.0, static_cast<double>(bin) / kPerOctave);
            const double l = q * rate / f;
            // librosa.filters.wavelet: samples arange(floor(-l/2), floor(l/2)), a periodic Hann over the first
            // floor(l) of them, normalised to unit L1; the response then over sqrt(the full-rate length).
            const int first = static_cast<int>(std::floor(-l / 2.0));
            const int count = static_cast<int>(std::floor(l / 2.0)) - first;
            const int span = static_cast<int>(std::floor(l));
            kr.assign(count, 0.0);
            ki.assign(count, 0.0);
            double l1 = 0.0;
            for (int m = 0; m < count; ++m) {
                const double w = m < span ? 0.5 - 0.5 * std::cos(kTau * m / span) : 0.0;
                const double phase = kTau * f * (first + m) / rate;
                kr[m] = w * std::cos(phase);
                ki[m] = w * std::sin(phase);
                l1 += w;
            }
            if (l1 <= 0.0) continue;
            const double scale = 1.0 / (l1 * std::sqrt(q * sampleRateHz / f));
            const int pc = ((bin % kPerOctave + 1) % kPerOctave) / 3;  // cq_to_chroma: 3 bins centred on each semitone
            for (int t = 0; t < frames; ++t) {
                const int64_t start = static_cast<int64_t>(t) * step + first;
                const int m0 = static_cast<int>(std::max<int64_t>(0, -start));
                const int m1 = static_cast<int>(std::min<int64_t>(count, length - start));
                double re = 0.0, im = 0.0;
                for (int m = m0; m < m1; ++m) {
                    const double v = x[start + m];
                    re += v * kr[m];
                    im += v * ki[m];
                }
                raw[t][pc] += std::sqrt(re * re + im * im) * scale;
            }
        }
    }
    std::vector<std::array<float, 12>> out(static_cast<size_t>(frames));
    for (int t = 0; t < frames; ++t) {
        const double peak = *std::max_element(raw[t].begin(), raw[t].end());
        for (int p = 0; p < 12; ++p) out[t][p] = peak > kTiny ? static_cast<float>(raw[t][p] / peak) : 0.0f;
    }
    return out;
}

void keyOf(const std::array<double, 12>& chroma, int& root, bool& major, float& clarity) {
    double cm = 0.0;
    for (double v : chroma) cm += v;
    cm /= 12.0;
    double best = -2.0;
    root = 0;
    major = true;
    for (int r = 0; r < 12; ++r)
        for (int mode = 0; mode < 2; ++mode) {
            // np.corrcoef(chroma, np.roll(profile, r)): profile[(p - r) mod 12] at pitch class p.
            double pm = 0.0;
            for (double v : kKk[mode]) pm += v;
            pm /= 12.0;
            double sab = 0.0, saa = 0.0, sbb = 0.0;
            for (int p = 0; p < 12; ++p) {
                const double a = chroma[p] - cm, b = kKk[mode][((p - r) % 12 + 12) % 12] - pm;
                sab += a * b;
                saa += a * a;
                sbb += b * b;
            }
            const double corr = saa > 0.0 ? sab / std::sqrt(saa * sbb) : -1.0;
            if (corr > best) {
                best = corr;
                root = r;
                major = mode == 0;
            }
        }
    clarity = static_cast<float>(std::clamp(best, 0.0, 1.0));
}

Features analyseFeatures(const float* mono, size_t samples, int sampleRateHz) {
    Features out;
    if (!mono || samples == 0 || sampleRateHz <= 0) return out;
    const Params params = paramsFor(sampleRateHz);
    const int n = params.fftSize, hop = params.hop, bins = n / 2 + 1;
    const int frames = 1 + static_cast<int>(samples / static_cast<size_t>(hop));
    const double frameRate = static_cast<double>(sampleRateHz) / hop;
    const double binHz = static_cast<double>(sampleRateHz) / n;
    out.sampleRateHz = sampleRateHz;
    out.hop = hop;
    out.frames = frames;
    for (auto* v : {&out.rms, &out.centroidHz, &out.rolloffHz, &out.flatness, &out.harmonic, &out.percussive})
        v->assign(static_cast<size_t>(frames), 0.0f);

    // Band edges, in bins.
    auto binOf = [&](double hz) { return std::clamp(static_cast<int>(std::lround(hz / binHz)), 0, bins); };
    constexpr double kBand[5][2] = {{20, 250}, {250, 4000}, {4000, 16000}, {20, 60}, {2000, 6000}};
    std::array<double, GEODE_SOUND_EQ_BANDS + 1> eqEdge{};
    for (int j = 0; j <= GEODE_SOUND_EQ_BANDS; ++j)  // geometric midpoints around centres 25 Hz … 22 kHz
        eqEdge[j] = 25.0 * std::pow(22000.0 / 25.0, (j - 0.5) / (GEODE_SOUND_EQ_BANDS - 1));
    std::array<double, GEODE_SOUND_SPECTRUM_POINTS + 1> specEdge{};
    for (int j = 0; j <= GEODE_SOUND_SPECTRUM_POINTS; ++j)
        specEdge[j] = 20.0 * std::pow(1000.0, (j - 0.5) / (GEODE_SOUND_SPECTRUM_POINTS - 1));
    constexpr double kDrumBand[3][2] = {{30, 150}, {150, 1000}, {7000, 16000}};  // kick, snare body, hat air

    std::vector<std::array<double, 5>> bandPower(static_cast<size_t>(frames));
    std::vector<std::array<double, GEODE_SOUND_EQ_BANDS>> eqPower(static_cast<size_t>(frames));
    std::vector<std::array<double, GEODE_SOUND_SPECTRUM_POINTS>> specAmp(static_cast<size_t>(frames));
    std::vector<double> hfc(static_cast<size_t>(frames)), peakAmp(static_cast<size_t>(frames)), rmsSq(static_cast<size_t>(frames));
    std::array<std::vector<double>, 3> drumPower;
    for (auto& d : drumPower) d.assign(static_cast<size_t>(frames), 0.0);
    out.peakFreq.assign(static_cast<size_t>(frames), 0.0f);

    std::vector<float> window(n);
    for (int i = 0; i < n; ++i) window[i] = static_cast<float>(0.5 - 0.5 * std::cos(kTau * i / n));
    Fft fft(n);
    std::vector<float> frame(n);
    constexpr int kRing = 32, kMedian = 31, kReach = kMedian / 2;
    std::vector<float> ring(static_cast<size_t>(kRing) * bins);
    std::vector<float> column(kMedian), hMed(bins), pMed(bins);

    // librosa.decompose.hpss for the frame centred at c, once every frame it reaches (±15) is in the ring.
    auto separate = [&](int c) {
        const float* s = ring.data() + static_cast<size_t>(c % kRing) * bins;
        for (int k = 0; k < bins; ++k) {
            for (int d = 0; d < kMedian; ++d) column[d] = ring[static_cast<size_t>(reflect(c + d - kReach, frames) % kRing) * bins + k];
            hMed[k] = medianOf(column.data(), kMedian);
            for (int d = 0; d < kMedian; ++d) column[d] = s[reflect(k + d - kReach, bins)];
            pMed[k] = medianOf(column.data(), kMedian);
        }
        double eh = 0.0, ep = 0.0;
        std::array<double, 3> drums{};
        for (int k = 0; k < bins; ++k) {
            const double h = hMed[k], p = pMed[k];
            double mh = 0.5, mp = 0.5;  // split_zeros: margins are 1
            if (h > 0.0 || p > 0.0) {
                mh = h * h / (h * h + p * p);
                mp = p * p / (h * h + p * p);
            }
            const double hs = s[k] * mh, ps = s[k] * mp;
            eh += hs * hs;
            ep += ps * ps;
            const double hz = k * binHz;
            for (int d = 0; d < 3; ++d)
                if (hz >= kDrumBand[d][0] && hz < kDrumBand[d][1]) drums[d] += ps * ps;
        }
        out.harmonic[c] = static_cast<float>(eh);
        out.percussive[c] = static_cast<float>(ep);
        for (int d = 0; d < 3; ++d) drumPower[d][c] = drums[d];
    };

    for (int t = 0; t < frames + kReach; ++t) {
        if (t < frames) {
            const int64_t start = static_cast<int64_t>(t) * hop - n / 2;
            double sq = 0.0;
            for (int i = 0; i < n; ++i) {
                const int64_t at = start + i;
                const float v = at >= 0 && at < static_cast<int64_t>(samples) ? mono[at] : 0.0f;
                sq += static_cast<double>(v) * v;
                frame[i] = v * window[i];
            }
            out.rms[t] = static_cast<float>(std::sqrt(sq / n));
            rmsSq[t] = sq / n;
            float* mag = ring.data() + static_cast<size_t>(t % kRing) * bins;
            fft.magnitudes(frame.data(), mag);

            double sum = 0.0, weighted = 0.0, logSum = 0.0, powSum = 0.0, h = 0.0, peak = 0.0;
            int peakBin = 0;
            for (int k = 0; k < bins; ++k) {
                const double m = mag[k], p = m * m, hz = k * binHz;
                sum += m;
                weighted += m * hz;
                const double floored = std::max(1e-10, p);
                logSum += std::log(floored);
                powSum += floored;
                h += k * p;
                if (k > 0 && m > peak) peak = m, peakBin = k;
            }
            out.centroidHz[t] = sum > kTiny ? static_cast<float>(weighted / sum) : 0.0f;
            const double threshold = 0.85 * sum;
            double cum = 0.0;
            for (int k = 0; k < bins; ++k) {
                cum += mag[k];
                if (cum >= threshold) {
                    out.rolloffHz[t] = static_cast<float>(k * binHz);
                    break;
                }
            }
            out.flatness[t] = static_cast<float>(std::exp(logSum / bins) / (powSum / bins));
            hfc[t] = h;
            peakAmp[t] = peak;
            out.peakFreq[t] = logAxis(peakBin * binHz);

            // +6 dB/octave tilt (power × (f / 1 kHz)²), as live measures its bands.
            auto tilted = [&](int k) {
                const double hz = k * binHz / 1000.0;
                return static_cast<double>(mag[k]) * mag[k] * hz * hz;
            };
            for (int b = 0; b < 5; ++b) {
                double e = 0.0;
                for (int k = binOf(kBand[b][0]); k < binOf(kBand[b][1]); ++k) e += tilted(k);
                bandPower[t][b] = e;
            }
            for (int j = 0; j < GEODE_SOUND_EQ_BANDS; ++j) {
                double e = 0.0;
                for (int k = binOf(eqEdge[j]); k < std::max(binOf(eqEdge[j]) + 1, binOf(eqEdge[j + 1])) && k < bins; ++k) e += tilted(k);
                eqPower[t][j] = e;
            }
            for (int j = 0; j < GEODE_SOUND_SPECTRUM_POINTS; ++j) {
                double e = 0.0;
                for (int k = binOf(specEdge[j]); k < std::max(binOf(specEdge[j]) + 1, binOf(specEdge[j + 1])) && k < bins; ++k) e += tilted(k);
                specAmp[t][j] = std::sqrt(e);
            }
        }
        if (t >= kReach) separate(t - kReach);
    }

    // Onsets from the mel spectrogram: librosa's mean onset strength, and the whitened one the trigger reads.
    const MelSpectrogram mel = melSpectrogram(mono, samples, sampleRateHz, params);
    out.onsetMean = onsetStrength(mel, params, Aggregate::Mean);
    out.onsetWhitened.assign(static_cast<size_t>(frames), 0.0f);
    {
        // Each band over a peak that decays 60 dB in 10 s, floored 80 dB under the track's loudest band.
        const double memory = std::pow(10.0, -6.0 / (10.0 * frameRate));
        float loudest = -1e9f;
        for (float v : mel.db) loudest = std::max(loudest, v);
        const double floorPower = std::pow(10.0, (loudest - 80.0) / 10.0);
        const int delay = 1 + params.fftSize / (2 * params.hop);
        std::vector<double> peak(static_cast<size_t>(mel.mels), floorPower), prev(static_cast<size_t>(mel.mels), 0.0);
        for (int t = 0; t < mel.frames; ++t) {
            double flux = 0.0;
            for (int m = 0; m < mel.mels; ++m) {
                const double p = std::pow(10.0, mel.at(t, m) / 10.0);
                peak[m] = std::max({p, floorPower, memory * peak[m]});
                const double w = p / peak[m];
                if (t > 0) flux += std::max(0.0, w - prev[m]);
                prev[m] = w;
            }
            if (t > 0 && t - 1 + delay < frames) out.onsetWhitened[t - 1 + delay] = static_cast<float>(flux / mel.mels);
        }
    }
    out.onsets = pick(out.onsetWhitened, frameRate);
    out.kicks = pick(bandOnsets(drumPower[0]), frameRate);
    out.snares = pick(bandOnsets(drumPower[1]), frameRate);
    out.hats = pick(bandOnsets(drumPower[2]), frameRate);

    // Chroma and the key.
    out.chroma = cqtChroma(mono, samples, sampleRateHz, hop, frames);
    std::array<double, 12> mean{};
    for (const auto& c : out.chroma)
        for (int p = 0; p < 12; ++p) mean[p] += c[p] / frames;
    keyOf(mean, out.keyRoot, out.keyMajor, out.keyClarity);

    // The schema's scales.
    auto frames_ = [&](double seconds) { return std::max(1, static_cast<int>(std::lround(seconds * frameRate))); };
    const int rel = frames_(2.0);  // ±2 s: the 4 s window live measures against
    out.band.resize(static_cast<size_t>(frames));
    out.bandAtt.resize(static_cast<size_t>(frames));
    for (int b = 0; b < 5; ++b) {
        std::vector<double> e(static_cast<size_t>(frames));
        for (int t = 0; t < frames; ++t) e[t] = bandPower[t][b];
        const std::vector<float> r = relative(e, rel);
        std::vector<double> rd(r.begin(), r.end());
        const std::vector<double> att = centredMean(rd, 1);
        for (int t = 0; t < frames; ++t) {
            out.band[t][b] = r[t];
            out.bandAtt[t][b] = static_cast<float>(att[t]);
        }
    }
    {
        std::vector<double> amp(out.rms.begin(), out.rms.end());
        const std::vector<double> slowPeak = centredMax(amp, frames_(4.0));
        const std::vector<double> shortE = centredMean(rmsSq, frames_(0.2)), longE = centredMean(rmsSq, frames_(4.0));
        const std::vector<double> peakSlow = centredMax(peakAmp, frames_(2.0));
        out.level.resize(static_cast<size_t>(frames));
        out.energyTrend.resize(static_cast<size_t>(frames));
        out.peakMag.resize(static_cast<size_t>(frames));
        for (int t = 0; t < frames; ++t) {
            out.level[t] = slowPeak[t] > 1e-9 ? static_cast<float>(amp[t] / slowPeak[t]) : 0.0f;
            out.energyTrend[t] = longE[t] > 1e-12 ? static_cast<float>(std::clamp(0.5 * shortE[t] / longE[t], 0.0, 1.0)) : 0.5f;
            out.peakMag[t] = peakSlow[t] > 1e-12 ? static_cast<float>(peakAmp[t] / peakSlow[t]) : 0.0f;
        }
    }
    {
        std::vector<double> f(out.onsetMean.begin(), out.onsetMean.end());
        out.flux = relative(f, rel);
        out.hfc = relative(hfc, rel);
        constexpr float kFluxBand[3][2] = {{0.0f, 250.0f}, {250.0f, 4000.0f}, {4000.0f, 1e9f}};
        out.fluxBands.resize(static_cast<size_t>(frames));
        for (int b = 0; b < 3; ++b) {
            const std::vector<float> env = onsetStrength(mel, params, Aggregate::Mean, kFluxBand[b][0], kFluxBand[b][1]);
            const std::vector<float> r = relative(std::vector<double>(env.begin(), env.end()), rel);
            for (int t = 0; t < frames; ++t) out.fluxBands[t][b] = t < static_cast<int>(r.size()) ? r[t] : 0.0f;
        }
    }
    out.eq.resize(static_cast<size_t>(frames));
    for (int j = 0; j < GEODE_SOUND_EQ_BANDS; ++j) {
        std::vector<double> e(static_cast<size_t>(frames));
        for (int t = 0; t < frames; ++t) e[t] = eqPower[t][j];
        const std::vector<float> r = relative(e, rel);
        for (int t = 0; t < frames; ++t) out.eq[t][j] = r[t];
    }
    {
        std::vector<double> top(static_cast<size_t>(frames));
        for (int t = 0; t < frames; ++t) top[t] = *std::max_element(specAmp[t].begin(), specAmp[t].end());
        const std::vector<double> slow = centredMax(top, rel);
        out.spectrum.resize(static_cast<size_t>(frames));
        for (int t = 0; t < frames; ++t)
            for (int j = 0; j < GEODE_SOUND_SPECTRUM_POINTS; ++j)
                out.spectrum[t][j] = slow[t] > 1e-12 ? static_cast<float>(specAmp[t][j] / slow[t]) : 0.0f;
    }
    // Chroma at three speeds, each over its own maximum: medium ~0.5 s, fast ~0.1 s, slow ~4 s (live's).
    auto smoothChroma = [&](double seconds, std::vector<std::array<float, 12>>& dst) {
        dst.assign(static_cast<size_t>(frames), std::array<float, 12>{});
        for (int p = 0; p < 12; ++p) {
            std::vector<double> c(static_cast<size_t>(frames));
            for (int t = 0; t < frames; ++t) c[t] = out.chroma[t][p];
            const std::vector<double> m = centredMean(c, frames_(seconds / 2.0));
            for (int t = 0; t < frames; ++t) dst[t][p] = static_cast<float>(m[t]);
        }
        for (auto& c : dst) {
            const float peak = *std::max_element(c.begin(), c.end());
            for (float& v : c) v = peak > kTiny ? v / peak : 0.0f;
        }
    };
    smoothChroma(0.5, out.chromaMedium);
    smoothChroma(0.1, out.chromaFast);
    smoothChroma(4.0, out.chromaSlow);
    return out;
}

void sampleFeatures(const Features& f, double seconds, double dt, GeodeSoundFrame& out) {
    if (f.frames <= 0) return;
    const int t = std::clamp(static_cast<int>(std::lround(seconds * f.sampleRateHz / f.hop)), 0, f.frames - 1);
    const auto& b = f.band[t];
    const auto& a = f.bandAtt[t];
    out.bass = b[0], out.mid = b[1], out.treble = b[2], out.sub = b[3], out.midHigh = b[4];
    out.bassAtt = a[0], out.midAtt = a[1], out.trebleAtt = a[2], out.subAtt = a[3], out.midHighAtt = a[4];
    out.vol = (b[0] + b[1] + b[2]) / 3.0f;
    out.volAtt = (a[0] + a[1] + a[2]) / 3.0f;
    out.level = f.level[t];
    out.rms = f.rms[t];
    out.loudness = f.rms[t] > 0.0f ? std::clamp((20.0f * std::log10(f.rms[t]) + 60.0f) / 60.0f, 0.0f, 1.0f) : 0.0f;
    out.centroid = logAxis(f.centroidHz[t]);
    out.rolloff = logAxis(f.rolloffHz[t]);
    out.flatness = f.flatness[t] > 0.0f ? std::clamp((10.0f * std::log10(f.flatness[t]) + 30.0f) / 30.0f, 0.0f, 1.0f) : 0.0f;
    out.flux = f.flux[t];
    out.energyTrend = f.energyTrend[t];
    out.peakFreq = f.peakFreq[t];
    out.peakMag = f.peakMag[t];
    out.fluxLow = f.fluxBands[t][0], out.fluxMid = f.fluxBands[t][1], out.fluxHigh = f.fluxBands[t][2];
    out.hfc = f.hfc[t];

    // Triggers fire on the one frame whose span (seconds - dt, seconds] holds the event; hit counts and
    // envelopes are functions of song time alone.
    struct Drum {
        const std::vector<double>& events;
        float decay;
        float* trigger;
        float* envelope;
    };
    const Drum drums[4] = {{f.kicks, 0.18f, &out.kick, &out.kickEnv},
                           {f.snares, 0.15f, &out.snare, &out.snareEnv},
                           {f.hats, 0.08f, &out.hat, &out.hatEnv},
                           {f.onsets, 0.15f, &out.onset, &out.onsetEnv}};
    for (const Drum& d : drums) {
        const size_t now = countUpTo(d.events, seconds), before = countUpTo(d.events, seconds - dt);
        *d.trigger = now > before ? 1.0f : 0.0f;
        *d.envelope = now > 0 ? static_cast<float>(std::exp(-(seconds - d.events[now - 1]) / d.decay)) : 0.0f;
    }
    auto everyNth = [&](const std::vector<double>& events, size_t nth) {
        const size_t now = countUpTo(events, seconds), before = countUpTo(events, seconds - dt);
        for (size_t k = before; k < now; ++k)
            if ((k + 1) % nth == 0) return 1.0f;
        return 0.0f;
    };
    out.kick2 = everyNth(f.kicks, 2), out.kick4 = everyNth(f.kicks, 4), out.kick8 = everyNth(f.kicks, 8);
    out.kick16 = everyNth(f.kicks, 16), out.snare2 = everyNth(f.snares, 2), out.snare4 = everyNth(f.snares, 4);
    auto toggle = [&](const std::vector<double>& events) {
        const size_t hits = countUpTo(events, seconds);
        if (hits == 0) return 0.0f;
        const float target = static_cast<float>(hits % 2), from = 1.0f - target;
        const double eased = 1.0 - std::exp(-(seconds - events[hits - 1]) / 0.06);
        return static_cast<float>(from + (target - from) * eased);
    };
    out.kickToggle = toggle(f.kicks);
    out.snareToggle = toggle(f.snares);

    out.keyRoot = static_cast<float>(f.keyRoot) / 12.0f;
    out.keyMajor = f.keyMajor ? 1.0f : 0.0f;
    out.keyClarity = f.keyClarity;
    const int majorRoot = f.keyMajor ? f.keyRoot : (f.keyRoot + 3) % 12;  // a minor key sits on its relative major
    out.keyHue = static_cast<float>((majorRoot * 7) % 12) / 12.0f;
    for (int p = 0; p < 12; ++p) {
        out.chroma[p] = f.chromaMedium[t][p];
        out.chromaFast[p] = f.chromaFast[t][p];
        out.chromaSlow[p] = f.chromaSlow[t][p];
    }
    for (int j = 0; j < GEODE_SOUND_EQ_BANDS; ++j) out.eq[j] = f.eq[t][j];
    for (int j = 0; j < GEODE_SOUND_SPECTRUM_POINTS; ++j) out.spectrum[j] = f.spectrum[t][j];
}

}  // namespace geode::sound::offline
