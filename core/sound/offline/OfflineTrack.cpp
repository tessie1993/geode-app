#include "sound/offline/OfflineTrack.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "sound/Fft.hpp"
#include "sound/offline/OfflineMath.hpp"

namespace geode::sound::offline {

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kDigitalSilence = 1e-4f;  // live's: below it a frame is silence
constexpr double kTimeWrap = 3600.0;
constexpr double kCountWrap = 4096.0;
constexpr int kWavePoints = GEODE_SOUND_WAVEFORM_POINTS;
constexpr int kWaveSlack = 32;  // live's

int liveFftSize(int sampleRateHz) {
    int n = 256;
    while (static_cast<float>(n) < 0.040f * static_cast<float>(sampleRateHz)) n *= 2;
    return n;
}

void fillHeld(std::vector<float>& v, float none) {
    float last = kNaN;
    for (float& x : v) {
        if (std::isnan(x)) x = last;
        else last = x;
    }
    const auto first = std::find_if(v.begin(), v.end(), [](float x) { return !std::isnan(x); });
    const float lead = first == v.end() ? none : *first;
    for (float& x : v) {
        if (!std::isnan(x)) break;
        x = lead;
    }
}

// Live's asymmetric lag (`rise` seconds up, `fall` down), forwards and backwards, averaged.
std::vector<float> zeroPhaseLag(const std::vector<float>& x, float rise, float fall, double frameRate) {
    const size_t n = x.size();
    std::vector<float> fwd(n), back(n);
    const float dt = static_cast<float>(1.0 / frameRate);
    auto step = [&](float y, float v) {
        const float tau = v > y ? rise : fall;
        return tau <= 0.0f ? v : v + (y - v) * std::exp(-dt / tau);
    };
    float y = n ? x[0] : 0.0f;
    for (size_t i = 0; i < n; ++i) fwd[i] = y = step(y, x[i]);
    y = n ? x[n - 1] : 0.0f;
    for (size_t i = n; i-- > 0;) back[i] = y = step(y, x[i]);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = 0.5f * (fwd[i] + back[i]);
    return out;
}

// The eased per-hit value live keeps for toggles and randoms: from the last value to the new one, over 60 ms.
float easedValue(const std::vector<double>& at, const std::vector<float>& value, double t) {
    const size_t hits = math::countUpTo(at, t);
    if (hits == 0) return 0.0f;
    const float from = hits >= 2 ? value[hits - 2] : 0.0f, target = value[hits - 1];
    const double eased = 1.0 - std::exp(-(t - at[hits - 1]) / 0.06);
    return static_cast<float>(from + (target - from) * eased);
}

}  // namespace

GeodeSoundTuning offlineDefaultTuning() {
    GeodeSoundTuning t{};
    for (int i = 0; i < 3; ++i) {
        t.gain[i] = 1.0f;
        t.attack[i] = 0.03f;
        t.release[i] = 0.15f;
    }
    t.drum[0] = 0.6f;
    t.drum[1] = 0.5f;
    t.drum[2] = 0.5f;
    t.drumDecay[0] = 0.18f;
    t.drumDecay[1] = 0.15f;
    t.drumDecay[2] = 0.08f;
    t.reactivity = 1.0f;
    t.speed = 1.0f;
    t.onsetThreshold = 2.0f;
    t.hardCutLevel = 2.5f;
    t.hardCutHalfLife = 60.0f;
    t.pitchThreshold = 0.35f;
    t.noteHold = 0.08f;
    t.silenceDb = -60.0f;
    return t;
}

Track analyseTrack(const float* interleaved, size_t frames, int channels, int sampleRateHz) {
    return analyseTrack(interleaved, frames, channels, sampleRateHz, offlineDefaultTuning());
}

Track analyseTrack(const float* interleaved, size_t samples, int channels, int sampleRateHz, const GeodeSoundTuning& tuning) {
    Track out;
    if (!interleaved || samples == 0 || channels <= 0 || sampleRateHz <= 0) return out;
    std::vector<float> left(samples), right(samples), mono(samples);
    for (size_t i = 0; i < samples; ++i) {
        left[i] = interleaved[i * channels];
        right[i] = channels > 1 ? interleaved[i * channels + 1] : left[i];
        mono[i] = 0.5f * (left[i] + right[i]);
    }
    out.sampleRateHz = sampleRateHz;
    out.grid = analyse(mono.data(), samples, sampleRateHz);
    out.form = analyseForm(mono.data(), samples, sampleRateHz, out.grid);
    out.features = analyseFeatures(mono.data(), samples, sampleRateHz);
    const Features& f = out.features;
    out.frames = f.frames;
    out.frameRate = static_cast<double>(sampleRateHz) / f.hop;
    out.harmony = analyseHarmony(f);
    out.pitch = analysePitch(mono.data(), samples, sampleRateHz, f.hop, f.frames);
    const int frames = f.frames;
    const size_t nf = static_cast<size_t>(frames);
    const double rate = out.frameRate;

    // One pass at live's window size, centred on each frame: partial density and the stereo energies.
    const int n = liveFftSize(sampleRateHz), bins = n / 2 + 1;
    const double binHz = static_cast<double>(sampleRateHz) / n;
    const int from = std::clamp(static_cast<int>(std::ceil(100.0 / binHz)), 1, n / 2);
    const int to = std::clamp(static_cast<int>(std::ceil(10000.0 / binHz)), from + 1, n / 2 + 1);
    std::vector<float> window(static_cast<size_t>(n)), frame(static_cast<size_t>(n)), mag(static_cast<size_t>(bins));
    for (int i = 0; i < n; ++i) window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / n));
    Fft fft(n);
    std::vector<double> density(nf, 0.0), eL(nf, 0.0), eR(nf, 0.0), eMid(nf, 0.0), eSide(nf, 0.0);
    std::vector<uint8_t> sounding(nf, 0);
    for (int t = 0; t < frames; ++t) {
        const int64_t start = static_cast<int64_t>(t) * f.hop - n / 2;
        double sq = 0.0;
        for (int i = 0; i < n; ++i) {
            const int64_t at = start + i;
            const bool inside = at >= 0 && at < static_cast<int64_t>(samples);
            const double l = inside ? left[at] : 0.0, r = inside ? right[at] : 0.0, m = 0.5 * (l + r);
            frame[i] = static_cast<float>(m) * window[i];
            sq += m * m;
            eL[t] += l * l;
            eR[t] += r * r;
            eMid[t] += 0.25 * (l + r) * (l + r);
            eSide[t] += 0.25 * (l - r) * (l - r);
        }
        sounding[t] = std::sqrt(sq / n) >= kDigitalSilence;
        if (!sounding[t]) continue;
        fft.magnitudes(frame.data(), mag.data());
        float rangeMax = 0.0f;
        for (int k = from; k < to; ++k) rangeMax = std::max(rangeMax, mag[k]);
        int peaks = 0;
        for (int k = std::max(from, 1); k < std::min(to, n / 2); ++k)
            if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1] && mag[k] > 0.1f * rangeMax) ++peaks;
        density[t] = std::clamp(peaks / 48.0, 0.0, 1.0);
    }
    auto heldMean = [&](const std::vector<double>& x, double seconds) {
        const std::vector<double> m = math::centredMeanWhere(x, sounding, math::framesFor(seconds, rate), static_cast<double>(kNaN));
        std::vector<float> v(m.begin(), m.end());
        fillHeld(v, 0.0f);
        return v;
    };
    out.densitySlow = heldMean(density, 0.5);
    out.densityFast = heldMean(density, 0.05);
    {
        const int reach = math::framesFor(0.1, rate);
        const std::vector<double> l = math::centredMean(eL, reach), r = math::centredMean(eR, reach),
                                  mid = math::centredMean(eMid, reach), side = math::centredMean(eSide, reach);
        out.stereoWidth.assign(nf, kNaN);
        out.stereoPan.assign(nf, kNaN);
        for (int t = 0; t < frames; ++t) {
            if (!sounding[t]) continue;  // silence holds, as live
            if (mid[t] + side[t] > 0.0) out.stereoWidth[t] = std::clamp(static_cast<float>(side[t] / (mid[t] + side[t])), 0.0f, 1.0f);
            if (l[t] + r[t] > 0.0) out.stereoPan[t] = std::clamp(static_cast<float>((r[t] - l[t]) / (l[t] + r[t])), -1.0f, 1.0f);
        }
        fillHeld(out.stereoWidth, 0.0f);
        fillHeld(out.stereoPan, 0.0f);
    }

    // Presences, the unbounded mean level and intensity.
    for (int b = 0; b < 3; ++b) {
        std::vector<double> att(nf);
        for (int t = 0; t < frames; ++t) att[t] = f.bandAtt[t][b];
        const std::vector<double> slow = math::centredMean(att, math::framesFor(3.0, rate));
        out.presence[b].resize(nf);
        for (int t = 0; t < frames; ++t) out.presence[b][t] = std::clamp(static_cast<float>(0.5 * slow[t]), 0.0f, 1.0f);
    }
    out.rawVol.assign(nf, 0.0);
    out.energy.assign(nf, 0.0);
    for (int t = 0; t < frames; ++t) {
        double sum = 0.0;
        for (int b = 0; b < 3; ++b) sum += 4.0 * std::atanh(std::min(f.band[t][b], 3.9999f) / 4.0);  // undo the bend
        out.rawVol[t] = sum / 3.0;
        out.energy[t] = static_cast<double>(f.rms[t]) * f.rms[t];
    }
    {
        const std::vector<double> shortE = math::centredMean(out.energy, math::framesFor(0.2, rate)),
                                  longE = math::centredMean(out.energy, math::framesFor(4.0, rate));
        const double dt = 1.0 / rate;
        std::vector<double> fwd(nf), back(nf);
        auto creep = [&](double i, int t) {
            const double trend = longE[t] > 1e-12 ? shortE[t] / longE[t] : 1.0;
            return std::clamp(i + dt * (0.15 * std::max(0.0, trend - 1.0) - 0.03 * i), 0.0, 1.0);
        };
        double i = 0.0;
        for (int t = 0; t < frames; ++t) fwd[t] = i = creep(i, t);
        i = 0.0;
        for (int t = frames - 1; t >= 0; --t) back[t] = i = creep(i, t);
        out.intensity.resize(nf);
        for (int t = 0; t < frames; ++t) out.intensity[t] = static_cast<float>(0.5 * (fwd[t] + back[t]));
    }

    // The waveform's source: the mono mix block-averaged as live draws it, kept as 16-bit.
    out.waveBlock = std::max(1, n / 512);
    out.wave.resize(samples / static_cast<size_t>(out.waveBlock));
    for (size_t b = 0; b < out.wave.size(); ++b) {
        float s = 0.0f;
        for (int j = 0; j < out.waveBlock; ++j) s += mono[b * out.waveBlock + j];
        const float v = std::clamp(s / static_cast<float>(out.waveBlock), -1.0f, 1.0f);
        out.wave[b] = static_cast<int16_t>(std::lround(v * 32767.0f));
    }

    applyTuning(out, tuning);
    return out;
}

void applyTuning(Track& track, const GeodeSoundTuning& tuning) {
    track.tuning = tuning;
    const Features& f = track.features;
    const int frames = track.frames;
    if (frames <= 0) return;
    const size_t nf = static_cast<size_t>(frames);
    const double rate = track.frameRate, dt = 1.0 / rate;
    const float silenceOn = std::pow(10.0f, tuning.silenceDb / 20.0f);

    track.melody = analyseMelody(track.pitch, f, tuning.pitchThreshold, tuning.noteHold, tuning.silenceDb);

    // The TouchDesigner layer and the band clocks.
    for (int b = 0; b < 3; ++b) {
        std::vector<float> rel(nf);
        for (int t = 0; t < frames; ++t) rel[t] = f.band[t][b];
        const std::vector<float> lagged = zeroPhaseLag(rel, tuning.attack[b], tuning.release[b], rate);
        track.shaped[b].resize(nf);
        for (int t = 0; t < frames; ++t)
            track.shaped[b][t] = std::clamp(tuning.reactivity * tuning.gain[b] * (0.5f * lagged[t] - tuning.threshold[b]) + tuning.offset[b], 0.0f, 1.0f);
        track.bandClock[b].assign(nf + 1, 0.0);
        for (int t = 0; t < frames; ++t) track.bandClock[b][t + 1] = track.bandClock[b][t] + tuning.speed * dt * f.bandAtt[t][b];
    }
    track.curvedClock.assign(nf + 1, 0.0);
    for (int t = 0; t < frames; ++t) {
        const double volAtt = (f.bandAtt[t][0] + f.bandAtt[t][1] + f.bandAtt[t][2]) / 3.0;
        track.curvedClock[t + 1] = track.curvedClock[t] + tuning.speed * dt * std::pow(std::max(0.0, volAtt), 2.5);
    }
    {
        std::vector<double> above(nf);
        for (int t = 0; t < frames; ++t) above[t] = f.rms[t] > silenceOn ? 1.0 : 0.0;
        const std::vector<double> fade = math::centredMean(above, math::framesFor(1.0, rate));
        track.presenceFade.assign(fade.begin(), fade.end());
    }

    // Hits for hitTime, and the random sequence drawn on kicks and note changes in time order.
    track.hits = f.kicks;
    track.hits.insert(track.hits.end(), f.snares.begin(), f.snares.end());
    std::sort(track.hits.begin(), track.hits.end());
    track.kickRandomAt.clear();
    track.kickRandom.clear();
    track.noteRandomAt.clear();
    track.noteRandom.clear();
    {
        uint32_t state = 0x9E3779B9u;  // live's seed and generator
        auto next = [&] {
            state = state * 1664525u + 1013904223u;
            return static_cast<float>(state >> 8) / 16777216.0f;
        };
        size_t k = 0, m = 0;
        const std::vector<double>& notes = track.melody.notes;
        while (k < f.kicks.size() || m < notes.size()) {
            if (m >= notes.size() || (k < f.kicks.size() && f.kicks[k] <= notes[m])) {  // live: drums before melody
                track.kickRandomAt.push_back(f.kicks[k++]);
                track.kickRandom.push_back(next());
            } else {
                track.noteRandomAt.push_back(notes[m++]);
                track.noteRandom.push_back(next());
            }
        }
    }

    // Silence and return: runs under the silence level of at least half a second, ended above twice it.
    track.silences.clear();
    track.returns.clear();
    std::vector<uint8_t> silent(nf, 0);
    {
        const int hold = math::framesFor(0.5, rate);
        int below = -1;  // first frame of the current stretch under the level
        bool inRun = false;
        for (int t = 0; t < frames; ++t) {
            const float rms = f.rms[t];
            if (!inRun) {
                if (rms < silenceOn) {
                    if (below < 0) below = t;
                    if (t - below + 1 >= hold) {
                        inRun = true;
                        if (below > 0) track.silences.push_back(below / rate);  // the moment it stopped
                        for (int d = below; d <= t; ++d) silent[d] = 1;
                    }
                } else {
                    below = -1;
                }
            } else if (rms > 2.0f * silenceOn) {
                if ((t - below) / rate >= 1.0) track.returns.push_back(t / rate);
                inRun = false;
                below = -1;
            } else {
                silent[t] = 1;
            }
        }
    }

    // The hard cut, on the peak of each rise past the threshold.
    track.hardCuts.clear();
    {
        const double rest = tuning.hardCutLevel, halfLife = std::max(1.0f, tuning.hardCutHalfLife);
        const int ahead = math::framesFor(0.1, rate);
        double threshold = 2.0 * rest, since = 1e9;
        for (int t = 0; t < frames; ++t) {
            since += dt;
            if (!silent[t] && f.rms[t] >= silenceOn && track.rawVol[t] > threshold && since >= 1.0) {
                int peak = t;
                for (int d = t + 1; d <= std::min(frames - 1, t + ahead); ++d)
                    if (track.rawVol[d] > track.rawVol[peak]) peak = d;
                track.hardCuts.push_back(peak / rate);
                threshold *= 2.0;
                since = 0.0;
                t = peak;
                continue;
            }
            threshold = rest + (threshold - rest) * std::exp(-std::log(4.0) * dt / halfLife);
        }
    }

    // Surge: the next 0.4 s against the 8 s before, after a quieter passage.
    track.surges.clear();
    track.surgeStrength.clear();
    {
        const std::vector<double>& e = track.energy;
        std::vector<double> prefix(nf + 1, 0.0);
        for (int t = 0; t < frames; ++t) prefix[t + 1] = prefix[t] + e[t];
        auto mean = [&](int a, int b) {
            a = std::clamp(a, 0, frames);
            b = std::clamp(b, 0, frames);
            return b > a ? (prefix[b] - prefix[a]) / (b - a) : 0.0;
        };
        const int shortSpan = math::framesFor(0.4, rate), longSpan = math::framesFor(8.0, rate);
        const int quietSpan = math::framesFor(4.0, rate), half = math::framesFor(0.2, rate);
        std::vector<double> ratio(nf, 1.0), local(nf, 0.0);
        for (int t = 0; t < frames; ++t) {
            const double before = mean(t - longSpan, t);
            ratio[t] = before > 1e-12 ? mean(t, t + shortSpan) / before : 1.0;
            local[t] = mean(t - half, t + half + 1);
        }
        bool armed = true;
        for (int t = 0; t < frames; ++t) {
            if (!armed && ratio[t] < 1.1) armed = true;
            if (!armed || silent[t] || ratio[t] <= 1.6) continue;
            bool peak = true;
            for (int d = std::max(0, t - half); d <= std::min(frames - 1, t + half) && peak; ++d)
                peak = ratio[d] < ratio[t] || (ratio[d] == ratio[t] && d >= t);
            if (!peak) continue;
            const double before = mean(t - longSpan, t);
            bool quiet = false;
            for (int u = std::max(0, t - quietSpan); u < t && !quiet; ++u) quiet = before > 1e-12 && local[u] / before < 0.75;
            const size_t back = math::countUpTo(track.returns, t / rate);
            const bool returned = back > 0 && t / rate - track.returns[back - 1] < 1.0;
            if (!quiet || returned) continue;
            track.surges.push_back(t / rate);
            track.surgeStrength.push_back(std::clamp(static_cast<float>(0.5 * ratio[t]), 0.05f, 1.0f));
            armed = false;
        }
    }
}

void sampleTrack(const Track& track, double seconds, double dt, GeodeSoundFrame& out) {
    if (track.frames <= 0) return;
    sample(track.grid, seconds, dt, out);
    sample(track.form, track.grid, seconds, dt, out);  // after the grid: a section restarts the phrase
    sampleFeatures(track.features, seconds, dt, out);
    sampleHarmony(track.harmony, seconds, dt, out);
    sampleMelody(track.melody, seconds, dt, out);

    const int t = math::frameAt(seconds, track.frameRate, track.frames);
    const double rate = track.frameRate;
    out.low = track.shaped[0][t];
    out.middle = track.shaped[1][t];
    out.high = track.shaped[2][t];
    out.bassPresence = track.presence[0][t];
    out.midPresence = track.presence[1][t];
    out.treblePresence = track.presence[2][t];
    out.intensity = track.intensity[t];
    out.bassTime = math::integralAt(track.bandClock[0], seconds, rate, kTimeWrap);
    out.midTime = math::integralAt(track.bandClock[1], seconds, rate, kTimeWrap);
    out.trebleTime = math::integralAt(track.bandClock[2], seconds, rate, kTimeWrap);
    out.curvedTime = math::integralAt(track.curvedClock, seconds, rate, kTimeWrap);
    out.hitTime = math::easedCount(track.hits, seconds, 0.15, track.tuning.speed, kCountWrap);
    out.noteTime = math::easedCount(track.melody.notes, seconds, 0.15, track.tuning.speed, kCountWrap);
    out.presence = track.presenceFade[t];
    out.densitySlow = track.densitySlow[t];
    out.densityFast = track.densityFast[t];
    out.stereoWidth = track.stereoWidth[t];
    out.stereoPan = track.stereoPan[t];
    out.kickRandom = easedValue(track.kickRandomAt, track.kickRandom, seconds);
    out.noteRandom = easedValue(track.noteRandomAt, track.noteRandom, seconds);
    out.hardCut = math::firstIn(track.hardCuts, seconds, dt) >= 0 ? 1.0f : 0.0f;
    out.silence = math::firstIn(track.silences, seconds, dt) >= 0 ? 1.0f : 0.0f;
    out.returned = math::firstIn(track.returns, seconds, dt) >= 0 ? 1.0f : 0.0f;
    const int surge = math::firstIn(track.surges, seconds, dt);
    out.surge = surge >= 0 ? track.surgeStrength[surge] : 0.0f;

    // The waveform: the newest points + slack blocks before `seconds`, slid to the trigger.
    float raw[kWavePoints + kWaveSlack];
    const int64_t end = static_cast<int64_t>(std::floor(seconds * track.sampleRateHz)) / track.waveBlock;
    for (int p = 0; p < kWavePoints + kWaveSlack; ++p) {
        const int64_t b = end - (kWavePoints + kWaveSlack) + p;
        raw[p] = b >= 0 && b < static_cast<int64_t>(track.wave.size()) ? track.wave[b] / 32767.0f : 0.0f;
    }
    int best = kWaveSlack;
    float bestLevel = std::numeric_limits<float>::max();
    for (int d = 0; d <= kWaveSlack; ++d) {
        const float a = raw[d + kWavePoints / 2 - 1], b = raw[d + kWavePoints / 2];
        if (b <= a) continue;  // the middle must be rising
        const float level = std::fabs(b);
        if (level < bestLevel) {
            bestLevel = level;
            best = d;
        }
    }
    for (int i = 0; i < kWavePoints; ++i) out.waveform[i] = raw[i + best];
}

}  // namespace geode::sound::offline
