#include "sound/offline/OfflineBeats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

#include "sound/Fft.hpp"

// Each step names the librosa 0.11.0 function it ports and keeps its quirks where they change a number
// (the local score's same-mode loop, numpy's round-half-to-even, the tempogram's linear-ramp padding), so
// core/tests/offline_beats_test.cpp --goldens can hold the port to librosa's output on the lab's clips.

namespace geode::sound::offline {

namespace {

constexpr double kTau = 2.0 * std::numbers::pi;

double frac(double x) { return x - std::floor(x); }

// numpy's round: to the nearest integer, halves to even (the default floating-point rounding mode).
double roundEven(double x) { return std::nearbyint(x); }

// librosa.hz_to_mel / mel_to_hz, Slaney's scale (htk=False): linear to 1 kHz, logarithmic above.
constexpr double kMelSp = 200.0 / 3.0;
constexpr double kMinLogHz = 1000.0;
constexpr double kMinLogMel = kMinLogHz / kMelSp;  // 15
const double kLogStep = std::log(6.4) / 27.0;

double hzToMel(double hz) { return hz >= kMinLogHz ? kMinLogMel + std::log(hz / kMinLogHz) / kLogStep : hz / kMelSp; }
double melToHz(double mel) { return mel >= kMinLogMel ? kMinLogHz * std::exp(kLogStep * (mel - kMinLogMel)) : kMelSp * mel; }

struct MelBank {
    std::vector<float> weights;  // mels × bins
    std::vector<int> from, to;   // each band's non-zero bins, [from, to)
    std::vector<float> centre;
};

// librosa.filters.mel(sr, n_fft, n_mels, fmin=0, fmax=sr/2, htk=False, norm="slaney").
MelBank melBank(int sampleRateHz, int fftSize, int mels) {
    const int bins = fftSize / 2 + 1;
    const double lo = hzToMel(0.0), hi = hzToMel(0.5 * sampleRateHz);
    std::vector<double> edge(static_cast<size_t>(mels) + 2);
    for (int i = 0; i < mels + 2; ++i) edge[i] = melToHz(lo + (hi - lo) * i / (mels + 1));
    MelBank bank;
    bank.weights.assign(static_cast<size_t>(mels) * bins, 0.0f);
    bank.from.assign(mels, bins);
    bank.to.assign(mels, 0);
    bank.centre.resize(mels);
    for (int m = 0; m < mels; ++m) {
        const double lower = edge[m + 1] - edge[m], upper = edge[m + 2] - edge[m + 1];
        const double enorm = 2.0 / (edge[m + 2] - edge[m]);
        bank.centre[m] = static_cast<float>(edge[m + 1]);
        for (int k = 0; k < bins; ++k) {
            const double f = static_cast<double>(k) * sampleRateHz / fftSize;
            const double w = std::max(0.0, std::min((f - edge[m]) / lower, (edge[m + 2] - f) / upper)) * enorm;
            if (w <= 0.0) continue;
            bank.weights[static_cast<size_t>(m) * bins + k] = static_cast<float>(w);
            bank.from[m] = std::min(bank.from[m], k);
            bank.to[m] = std::max(bank.to[m], k + 1);
        }
    }
    return bank;
}

float median(std::vector<float>& v) {
    const size_t n = v.size();
    if (n == 0) return 0.0f;
    std::nth_element(v.begin(), v.begin() + n / 2, v.end());
    const float upper = v[n / 2];
    if (n % 2) return upper;
    const float lower = *std::max_element(v.begin(), v.begin() + n / 2);
    return 0.5f * (lower + upper);
}

}  // namespace

Params paramsFor(int sampleRateHz) {
    const int scale = std::max(1, static_cast<int>(std::lround(sampleRateHz / 22050.0)));
    Params p;
    p.fftSize = 2048 * scale;
    p.hop = 512 * scale;
    return p;
}

// librosa.feature.melspectrogram(power=2, center=True, pad_mode="constant") then librosa.power_to_db.
MelSpectrogram melSpectrogram(const float* mono, size_t samples, int sampleRateHz, const Params& params) {
    const int n = params.fftSize, hop = params.hop, bins = n / 2 + 1;
    MelSpectrogram out;
    out.mels = params.mels;
    out.frames = 1 + static_cast<int>(samples / static_cast<size_t>(hop));
    const MelBank bank = melBank(sampleRateHz, n, params.mels);
    out.centreHz = bank.centre;

    std::vector<float> window(n);  // periodic Hann (scipy get_window("hann", n, fftbins=True))
    for (int i = 0; i < n; ++i) window[i] = static_cast<float>(0.5 - 0.5 * std::cos(kTau * i / n));
    Fft fft(n);
    std::vector<float> frame(n), mag(bins);
    std::vector<double> power(bins);
    out.db.assign(static_cast<size_t>(out.frames) * out.mels, 0.0f);
    float peak = -std::numeric_limits<float>::infinity();
    for (int t = 0; t < out.frames; ++t) {
        const int64_t start = static_cast<int64_t>(t) * hop - n / 2;
        for (int i = 0; i < n; ++i) {
            const int64_t at = start + i;
            frame[i] = at >= 0 && at < static_cast<int64_t>(samples) ? mono[at] * window[i] : 0.0f;
        }
        fft.magnitudes(frame.data(), mag.data());
        for (int k = 0; k < bins; ++k) power[k] = static_cast<double>(mag[k]) * mag[k];
        for (int m = 0; m < out.mels; ++m) {
            double s = 0.0;
            const float* w = bank.weights.data() + static_cast<size_t>(m) * bins;
            for (int k = bank.from[m]; k < bank.to[m]; ++k) s += w[k] * power[k];
            const float db = static_cast<float>(10.0 * std::log10(std::max(1e-10, s)));
            out.db[static_cast<size_t>(t) * out.mels + m] = db;
            peak = std::max(peak, db);
        }
    }
    const float floorDb = peak - 80.0f;  // top_db: the whole track's loudest bin sets the floor
    for (float& v : out.db) v = std::max(v, floorDb);
    return out;
}

// librosa.onset.onset_strength(lag=1, max_size=1, center=True, detrend=False).
std::vector<float> onsetStrength(const MelSpectrogram& mel, const Params& params, Aggregate aggregate, float fromHz,
                                 float belowHz) {
    std::vector<float> env(static_cast<size_t>(mel.frames), 0.0f);
    std::vector<int> bands;
    for (int m = 0; m < mel.mels; ++m)
        if (mel.centreHz[m] >= fromHz && mel.centreHz[m] < belowHz) bands.push_back(m);
    if (bands.empty()) return env;
    // The difference at spectrogram frame j (frame j+1 minus j) lands at j + lag + n_fft / (2·hop): librosa
    // pads the front by that much so a peak sits on the frame whose window is centred on the onset.
    const int delay = 1 + params.fftSize / (2 * params.hop);
    std::vector<float> diffs(bands.size());
    for (int j = 0; j + 1 < mel.frames && j + delay < mel.frames; ++j) {
        for (size_t b = 0; b < bands.size(); ++b) diffs[b] = std::max(0.0f, mel.at(j + 1, bands[b]) - mel.at(j, bands[b]));
        float value;
        if (aggregate == Aggregate::Median) {
            value = median(diffs);
        } else {
            double sum = 0.0;
            for (float d : diffs) sum += d;
            value = static_cast<float>(sum / static_cast<double>(diffs.size()));
        }
        env[static_cast<size_t>(j + delay)] = value;
    }
    return env;
}

// librosa.feature.tempo(onset_envelope, ac_size=8, start_bpm, std_bpm=1, max_tempo=320, aggregate=mean)
// over librosa.feature.tempogram(win_length, center=True, window="hann", norm=inf).
double estimateTempo(const std::vector<float>& envelope, int sampleRateHz, int hop, double startBpm) {
    const int n = static_cast<int>(envelope.size());
    if (n == 0 || std::none_of(envelope.begin(), envelope.end(), [](float v) { return v != 0.0f; })) return 0.0;
    const int win = static_cast<int>(static_cast<int64_t>(8.0 * sampleRateHz) / hop);  // time_to_frames(8 s)
    if (win < 2) return 0.0;
    const int pad = win / 2;
    // np.pad(mode="linear_ramp", end_values=0): ramps from 0 at the outer edge to the envelope's end value.
    std::vector<double> padded(static_cast<size_t>(n) + 2 * pad);
    for (int i = 0; i < pad; ++i) padded[i] = envelope.front() * static_cast<double>(i) / pad;
    for (int i = 0; i < n; ++i) padded[pad + i] = envelope[i];
    for (int j = 0; j < pad; ++j) padded[pad + n + j] = envelope.back() * (1.0 - static_cast<double>(j + 1) / pad);

    std::vector<double> window(win);
    for (int i = 0; i < win; ++i) window[i] = 0.5 - 0.5 * std::cos(kTau * i / win);
    std::vector<double> mean(win, 0.0), x(win), ac(win);
    for (int t = 0; t < n; ++t) {
        for (int i = 0; i < win; ++i) x[i] = padded[t + i] * window[i];
        for (int lag = 0; lag < win; ++lag) {
            double s = 0.0;
            for (int i = 0; i + lag < win; ++i) s += x[i] * x[i + lag];
            ac[lag] = s;
        }
        // norm=inf: over the largest |ac|, which is ac[0]; a silent window stays zero.
        const double scale = ac[0] > std::numeric_limits<float>::min() ? 1.0 / ac[0] : 1.0;
        for (int lag = 0; lag < win; ++lag) mean[lag] += ac[lag] * scale;
    }
    int best = -1;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (int lag = 1; lag < win; ++lag) {
        const double bpm = 60.0 * sampleRateHz / (static_cast<double>(hop) * lag);
        if (bpm >= 320.0) continue;  // max_tempo: everything faster gets a prior of -inf
        const double octaves = std::log2(bpm) - std::log2(startBpm);
        const double score = std::log1p(1e6 * mean[lag] / n) - 0.5 * octaves * octaves;
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    return best > 0 ? 60.0 * sampleRateHz / (static_cast<double>(hop) * best) : 0.0;
}

// librosa.beat.__beat_tracker: normalise, local score, dynamic programme, last beat, backtrack, trim.
std::vector<int> trackBeats(const std::vector<float>& envelope, double bpm, double frameRate, double tightness,
                            bool trim) {
    const int n = static_cast<int>(envelope.size());
    if (n < 2 || bpm <= 0.0 || std::none_of(envelope.begin(), envelope.end(), [](float v) { return v != 0.0f; }))
        return {};
    const double fpb = roundEven(frameRate * 60.0 / bpm);
    if (fpb < 1.0) return {};

    // __normalize_onsets: over the standard deviation (ddof=1).
    double mean = 0.0;
    for (float v : envelope) mean += v;
    mean /= n;
    double var = 0.0;
    for (float v : envelope) var += (v - mean) * (v - mean);
    const double sd = std::sqrt(var / (n - 1)) + std::numeric_limits<float>::min();
    std::vector<double> onset(n);
    for (int i = 0; i < n; ++i) onset[i] = envelope[i] / sd;

    // __beat_local_score: a Gaussian of ±fpb frames, σ = fpb/32, as librosa's own same-mode loop, which
    // leaves out the k = i + K/2 tap (the envelope's first sample under the window); kept for agreement.
    const int half = static_cast<int>(fpb);
    const int taps = 2 * half + 1;
    std::vector<double> gauss(taps);
    for (int k = 0; k < taps; ++k) {
        const double x = (k - half) * 32.0 / fpb;
        gauss[k] = std::exp(-0.5 * x * x);
    }
    std::vector<double> local(n, 0.0);
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        for (int k = std::max(0, i + half - n + 1); k < std::min(i + half, taps); ++k) s += gauss[k] * onset[i + half - k];
        local[i] = s;
    }

    // __beat_track_dp: the best predecessor between 2 beats and half a beat back, penalised by tightness ×
    // the squared log ratio of the gap to the beat period.
    std::vector<int> backlink(n, -1);
    std::vector<double> cum(n, 0.0);
    const double threshold = 0.01 * *std::max_element(local.begin(), local.end());
    bool firstBeat = true;
    const int nearest = static_cast<int>(roundEven(fpb / 2.0));
    const int farthest = static_cast<int>(2.0 * fpb);
    const double logFpb = std::log(fpb);
    for (int i = 0; i < n; ++i) {
        double bestScore = -std::numeric_limits<double>::infinity();
        int at = -1;
        for (int loc = i - nearest; loc >= i - farthest; --loc) {
            if (loc < 0) break;
            const double gap = std::log(static_cast<double>(i - loc)) - logFpb;
            const double score = cum[loc] - tightness * gap * gap;
            if (score > bestScore) {
                bestScore = score;
                at = loc;
            }
        }
        cum[i] = at >= 0 ? local[i] + bestScore : local[i];
        if (firstBeat && local[i] < threshold) {
            backlink[i] = -1;
        } else {
            backlink[i] = at;
            firstBeat = false;
        }
    }

    // __last_beat: the last local maximum of the cumulative score that reaches half their median.
    std::vector<double> peaks;
    std::vector<char> isPeak(n, 0);
    for (int i = 0; i < n; ++i) {
        const double prev = cum[i > 0 ? i - 1 : 0], next = cum[i + 1 < n ? i + 1 : n - 1];
        if (cum[i] > prev && cum[i] >= next) {
            isPeak[i] = 1;
            peaks.push_back(cum[i]);
        }
    }
    int tail = n - 1;
    if (!peaks.empty()) {
        std::sort(peaks.begin(), peaks.end());
        const size_t m = peaks.size();
        const double med = m % 2 ? peaks[m / 2] : 0.5 * (peaks[m / 2 - 1] + peaks[m / 2]);
        for (int i = n - 1; i >= 0; --i)
            if (isPeak[i] && cum[i] >= 0.5 * med) {
                tail = i;
                break;
            }
    }

    std::vector<int> beats;  // __dp_backtrack
    for (int i = tail; i >= 0; i = backlink[i]) beats.push_back(i);
    std::reverse(beats.begin(), beats.end());

    // __trim_beats: drop beats before the first and after the last frame whose local score passes half
    // the RMS of the beats' local scores smoothed by a 5-point Hann. librosa slices the full convolution
    // as [2 : frames + 2], and frames far outnumber beats, so the slice keeps the last two outputs of
    // the tail too (half the last beat's score, then 0): b + 2 values, not b. Kept for agreement.
    double limit = 0.0;
    if (trim && !beats.empty()) {
        constexpr double kHann5[5] = {0.0, 0.5, 1.0, 0.5, 0.0};
        const int b = static_cast<int>(beats.size());
        const int outputs = std::min(b + 2, n);
        double sumSq = 0.0;
        for (int i = 0; i < outputs; ++i) {
            double s = 0.0;
            for (int j = 0; j < 5; ++j) {
                const int at = i + 2 - j;
                if (at >= 0 && at < b) s += local[beats[at]] * kHann5[j];
            }
            sumSq += s * s;
        }
        limit = 0.5 * std::sqrt(sumSq / outputs);
    }
    int lo = 0, hi = n - 1;
    while (lo < n && local[lo] <= limit) ++lo;
    while (hi >= 0 && local[hi] <= limit) --hi;
    std::vector<int> kept;
    for (int f : beats)
        if (f >= lo && f <= hi) kept.push_back(f);
    return kept;
}

int downbeatPhase(const MelSpectrogram& mel, const Params& params, const std::vector<float>& envelope,
                  const std::vector<int>& beatFrames, int beatsPerBar) {
    const int beats = static_cast<int>(beatFrames.size());
    if (beatsPerBar < 2 || beats < 2 * beatsPerBar) return 0;

    // Harmony: for each candidate phase, the bars it implies, each as its mean mid-band (150 Hz–4 kHz)
    // log spectrum, and how far each bar is from the one before. The drums repeat every bar whatever the
    // phase, so they cancel out; the harmony, which changes on the bar line, does not.
    std::vector<int> mid;
    for (int m = 0; m < mel.mels; ++m)
        if (mel.centreHz[m] >= 150.0f && mel.centreHz[m] < 4000.0f) mid.push_back(m);
    std::vector<double> harmony(beatsPerBar, 0.0);
    for (int p = 0; p < beatsPerBar && !mid.empty(); ++p) {
        std::vector<double> prev, cur(mid.size());
        double distance = 0.0;
        int pairs = 0;
        for (int start = p; start + beatsPerBar < beats; start += beatsPerBar) {
            const int f0 = beatFrames[start], f1 = std::min(beatFrames[start + beatsPerBar], mel.frames);
            if (f1 <= f0) continue;
            std::fill(cur.begin(), cur.end(), 0.0);
            for (int f = f0; f < f1; ++f)
                for (size_t b = 0; b < mid.size(); ++b) cur[b] += mel.at(f, mid[b]);
            for (double& v : cur) v /= (f1 - f0);
            if (!prev.empty()) {
                double d = 0.0;
                for (size_t b = 0; b < cur.size(); ++b) d += (cur[b] - prev[b]) * (cur[b] - prev[b]);
                distance += std::sqrt(d / static_cast<double>(cur.size()));
                ++pairs;
            }
            prev = cur;
        }
        harmony[p] = pairs ? distance / pairs : 0.0;
    }

    // Accent: each beat's onset strength and its low-band (< 150 Hz) onset, each over its mean at the
    // beats, averaged per phase. A click track or a drum loop with no harmony is decided here.
    const std::vector<float> low = onsetStrength(mel, params, Aggregate::Mean, 0.0f, 150.0f);
    auto peakNear = [&](const std::vector<float>& v, int frame) {
        float best = 0.0f;
        for (int f = std::max(0, frame - 2); f <= std::min(static_cast<int>(v.size()) - 1, frame + 2); ++f) best = std::max(best, v[f]);
        return best;
    };
    std::vector<double> env(beats), bass(beats);
    double envMean = 0.0, bassMean = 0.0;
    for (int i = 0; i < beats; ++i) {
        env[i] = peakNear(envelope, beatFrames[i]);
        bass[i] = peakNear(low, beatFrames[i]);
        envMean += env[i];
        bassMean += bass[i];
    }
    envMean /= beats;
    bassMean /= beats;
    std::vector<double> accent(beatsPerBar, 0.0);
    std::vector<int> count(beatsPerBar, 0);
    for (int i = 0; i < beats; ++i) {
        double a = 0.0;
        if (envMean > 0.0) a += env[i] / envMean;
        if (bassMean > 0.0) a += bass[i] / bassMean;
        accent[i % beatsPerBar] += a;
        ++count[i % beatsPerBar];
    }
    for (int p = 0; p < beatsPerBar; ++p) accent[p] = count[p] ? accent[p] / count[p] : 0.0;

    const double maxHarmony = *std::max_element(harmony.begin(), harmony.end());
    const double maxAccent = *std::max_element(accent.begin(), accent.end());
    int best = 0;
    double bestScore = -1.0;
    for (int p = 0; p < beatsPerBar; ++p) {
        const double score = (maxHarmony > 0.0 ? harmony[p] / maxHarmony : 0.0) + (maxAccent > 0.0 ? accent[p] / maxAccent : 0.0);
        if (score > bestScore) {
            bestScore = score;
            best = p;
        }
    }
    return best;
}

BeatGrid analyse(const float* mono, size_t samples, int sampleRateHz) {
    BeatGrid grid;
    if (!mono || samples == 0 || sampleRateHz <= 0) return grid;
    const Params params = paramsFor(sampleRateHz);
    const MelSpectrogram mel = melSpectrogram(mono, samples, sampleRateHz, params);
    const std::vector<float> envelope = onsetStrength(mel, params, Aggregate::Median);
    grid.bpm = estimateTempo(envelope, sampleRateHz, params.hop);
    if (grid.bpm <= 0.0) return grid;
    const double frameRate = static_cast<double>(sampleRateHz) / params.hop;
    const std::vector<int> frames = trackBeats(envelope, grid.bpm, frameRate);
    for (int f : frames) grid.beats.push_back(static_cast<double>(f) * params.hop / sampleRateHz);
    grid.downbeatPhase = downbeatPhase(mel, params, envelope, frames, grid.beatsPerBar);
    for (size_t i = static_cast<size_t>(grid.downbeatPhase); i < grid.beats.size(); i += grid.beatsPerBar)
        grid.downbeats.push_back(grid.beats[i]);
    return grid;
}

void sample(const BeatGrid& grid, double seconds, double dt, GeodeSoundFrame& out) {
    constexpr double kWrap = 4096.0;
    const auto& b = grid.beats;
    const int bpb = std::max(1, grid.beatsPerBar);
    out.beat = out.downbeat = out.phrase = 0.0f;
    out.bpm = static_cast<float>(grid.bpm);
    if (b.size() < 2) {
        // No grid: the phases run on the tempo alone, if there is one, and nothing fires.
        const double pos = grid.bpm > 0.0 ? seconds * grid.bpm / 60.0 : 0.0;
        out.beatConf = 0.0f;
        out.beatPhase = static_cast<float>(frac(pos));
        out.barPhase = static_cast<float>(frac(pos / bpb));
        out.phrasePhase = static_cast<float>(frac(pos / (8.0 * bpb)));
        out.beatCount = out.barCount = out.phraseIndex = 0.0f;
        return;
    }
    const double period = grid.bpm > 0.0 ? 60.0 / grid.bpm : (b.back() - b.front()) / static_cast<double>(b.size() - 1);
    const size_t i = static_cast<size_t>(std::upper_bound(b.begin(), b.end(), seconds) - b.begin());  // beats so far
    double pos;  // in beats from the first, fractional
    if (i == 0) {
        pos = (seconds - b.front()) / period;
    } else if (i >= b.size()) {
        pos = static_cast<double>(b.size() - 1) + (seconds - b.back()) / period;
    } else {
        const double gap = b[i] - b[i - 1];
        pos = static_cast<double>(i - 1) + (seconds - b[i - 1]) / gap;
        out.bpm = static_cast<float>(60.0 / gap);
    }
    const bool inside = seconds >= b.front() && seconds <= b.back() + period;
    out.beatConf = inside ? 1.0f : 0.0f;
    out.beatPhase = static_cast<float>(frac(pos));

    const int phase = grid.downbeatPhase;
    auto barOf = [&](double beatPos) { return (beatPos - phase) / bpb; };
    const size_t first = static_cast<size_t>(std::upper_bound(b.begin(), b.end(), seconds - dt) - b.begin());
    for (size_t k = first; k < i; ++k) {
        out.beat = 1.0f;
        const int64_t rel = static_cast<int64_t>(k) - phase;
        if (rel >= 0 && rel % bpb == 0) {
            out.downbeat = 1.0f;
            if ((rel / bpb) % 8 == 0) out.phrase = 1.0f;
        }
    }
    const double bars = barOf(pos);
    const int64_t downbeatsSoFar = static_cast<int64_t>(i) > phase ? (static_cast<int64_t>(i) - 1 - phase) / bpb + 1 : 0;
    out.beatCount = static_cast<float>(std::fmod(static_cast<double>(i), kWrap));
    out.barCount = static_cast<float>(std::fmod(static_cast<double>(downbeatsSoFar), kWrap));
    out.barPhase = static_cast<float>(frac(bars));
    out.phraseIndex = static_cast<float>(std::fmod(std::max(0.0, std::floor(bars / 8.0)), kWrap));
    out.phrasePhase = static_cast<float>(frac(bars / 8.0));
}

}  // namespace geode::sound::offline
