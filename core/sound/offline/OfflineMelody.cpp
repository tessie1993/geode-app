#include "sound/offline/OfflineMelody.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numbers>

#include "sound/Fft.hpp"
#include "sound/offline/OfflineMath.hpp"

namespace geode::sound::offline {

namespace {

constexpr int kHarmonics = 5;  // live's harmonic sum
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// Live's frame FFT: about 40 ms at any rate (SoundEngine::fftSizeFor).
int liveFftSize(int sampleRateHz) {
    int n = 256;
    while (static_cast<float>(n) < 0.040f * static_cast<float>(sampleRateHz)) n *= 2;
    return n;
}

// NaN entries take the last value before them, and leading ones the first value after: "held".
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

struct Note {
    int start;  // frames, [start, end)
    int end;
    int note;   // MIDI
    int run;    // which line (a stretch of voiced frames with gaps under 0.15 s)
};

}  // namespace

PitchFrames analysePitch(const float* mono, size_t samples, int sampleRateHz, int hop, int frames) {
    PitchFrames out;
    if (!mono || samples == 0 || sampleRateHz <= 0 || hop <= 0 || frames <= 0) return out;
    out.frameRate = static_cast<double>(sampleRateHz) / hop;
    const size_t nf = static_cast<size_t>(frames);
    out.midi.assign(nf, 0.0f);
    out.confidence.assign(nf, 0.0f);
    out.evenness.assign(nf, 0.0f);
    const int p = 2 * liveFftSize(sampleRateHz);
    const float binHz = static_cast<float>(sampleRateHz) / static_cast<float>(p);
    const int top = p / 2 - 1;
    const int kMin = std::max(2, static_cast<int>(std::ceil(80.0f / binHz)));
    const int kMax = std::min(static_cast<int>(1500.0f / binHz), top / kHarmonics - 1);
    if (kMax < kMin) return out;
    std::vector<float> window(static_cast<size_t>(p)), frame(static_cast<size_t>(p)), mag(static_cast<size_t>(p / 2 + 1));
    for (int i = 0; i < p; ++i) window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / p));
    Fft fft(p);
    auto near = [&](int k) {
        if (k >= top || k < 1) return 0.0f;
        return std::max({mag[k - 1], mag[k], mag[k + 1]});
    };
    for (int t = 0; t < frames; ++t) {
        const int64_t start = static_cast<int64_t>(t) * hop - p / 2;
        for (int i = 0; i < p; ++i) {
            const int64_t at = start + i;
            frame[i] = at >= 0 && at < static_cast<int64_t>(samples) ? mono[at] * window[i] : 0.0f;
        }
        fft.magnitudes(frame.data(), mag.data());
        int best = kMin;
        float bestScore = 0.0f;
        double total = 0.0;
        for (int k = kMin; k <= kMax; ++k) {
            float score = mag[k];
            for (int h = 2; h <= kHarmonics; ++h) score += near(h * k) / static_cast<float>(h);
            total += score;
            if (score > bestScore) {
                bestScore = score;
                best = k;
            }
        }
        const float mean = static_cast<float>(total / (kMax - kMin + 1));
        out.confidence[t] = mean > 0.0f ? std::clamp((bestScore / mean - 3.0f) / 7.0f, 0.0f, 1.0f) : 0.0f;
        const float a = mag[best - 1], b = mag[best], c = mag[best + 1];
        const float curve = a - 2.0f * b + c;
        const float delta = curve < 0.0f ? std::clamp(0.5f * (a - c) / curve, -0.5f, 0.5f) : 0.0f;
        const float k0 = static_cast<float>(best) + delta;
        out.midi[t] = 69.0f + 12.0f * std::log2(k0 * binHz / 440.0f);
        float even = 0.0f, all = 0.0f;
        for (int h = 1; h <= 6; ++h) {
            const float amp = near(static_cast<int>(std::lround(k0 * h)));
            all += amp;
            if (h % 2 == 0) even += amp;
        }
        out.evenness[t] = all > 0.0f ? even / all : 0.0f;
    }
    return out;
}

MelodyTrack analyseMelody(const PitchFrames& pitch, const Features& features, float pitchThreshold, float noteHold,
                          float silenceDb) {
    MelodyTrack out;
    const int frames = std::min(static_cast<int>(pitch.midi.size()), features.frames);
    if (frames <= 0 || pitch.frameRate <= 0.0) return out;
    const double rate = pitch.frameRate;
    out.frameRate = rate;
    const size_t nf = static_cast<size_t>(frames);
    const float silenceOn = std::pow(10.0f, silenceDb / 20.0f);

    std::vector<uint8_t> voiced(nf, 0);
    for (int t = 0; t < frames; ++t) voiced[t] = pitch.confidence[t] >= pitchThreshold && features.rms[t] > silenceOn;

    // Octave slips: a reading an octave off the median of the voiced quarter second around it is folded back.
    std::vector<float> midi(pitch.midi.begin(), pitch.midi.begin() + frames);
    {
        const int reach = math::framesFor(0.25, rate);
        std::vector<float> around;
        std::vector<float> folded = midi;
        for (int t = 0; t < frames; ++t) {
            if (!voiced[t]) continue;
            around.clear();
            for (int d = std::max(0, t - reach); d <= std::min(frames - 1, t + reach); ++d)
                if (voiced[d]) around.push_back(midi[d]);
            std::nth_element(around.begin(), around.begin() + around.size() / 2, around.end());
            const float med = around[around.size() / 2];
            if (std::fabs(midi[t] - med - 12.0f) < 0.75f) folded[t] = midi[t] - 12.0f;
            else if (std::fabs(midi[t] - med + 12.0f) < 0.75f) folded[t] = midi[t] + 12.0f;
        }
        // A centred 3-frame median over the voiced neighbours steadies the line without moving its steps.
        for (int t = 0; t < frames; ++t) {
            if (!voiced[t]) continue;
            float v[3] = {0.0f, 0.0f, 0.0f};
            int n = 0;
            for (int d = std::max(0, t - 1); d <= std::min(frames - 1, t + 1); ++d)
                if (voiced[d]) v[n++] = folded[d];
            std::sort(v, v + n);
            midi[t] = v[n / 2];
        }
    }

    // Raw notes: live's rounding with 0.75-semitone hysteresis, lines bridged over gaps under 0.15 s.
    const int bridge = math::framesFor(0.15, rate);
    std::vector<Note> raw;
    {
        int cur = -1, last = -1000000, run = -1;
        for (int t = 0; t < frames; ++t) {
            if (!voiced[t]) continue;
            const bool fresh = t - last - 1 > bridge;
            if (fresh) {
                ++run;
                cur = -1;
            }
            const int n = cur >= 0 && std::fabs(midi[t] - static_cast<float>(cur)) < 0.75f ? cur : static_cast<int>(std::lround(midi[t]));
            if (n != cur || fresh) {
                raw.push_back({t, t + 1, n, run});
                cur = n;
            } else {
                raw.back().end = t + 1;
            }
            last = t;
        }
    }
    // Notes shorter than live's hold join their neighbour.
    std::vector<Note> notes;
    {
        int pendingStart = -1, pendingRun = -1;
        for (Note seg : raw) {
            if (pendingStart >= 0 && pendingRun == seg.run) seg.start = pendingStart;
            pendingStart = -1;
            const bool continues = !notes.empty() && notes.back().run == seg.run;
            const float leap = continues ? static_cast<float>(std::abs(seg.note - notes.back().note)) : 0.0f;
            const float hold = noteHold * (1.0f + std::max(0.0f, leap - 2.0f) / 5.0f);
            const bool shortNote = static_cast<double>(seg.end - seg.start) / rate < hold;
            if (continues && (shortNote || seg.note == notes.back().note)) {
                notes.back().end = seg.end;
            } else if (!continues && shortNote) {
                pendingStart = seg.start;
                pendingRun = seg.run;
            } else {
                notes.push_back(seg);
            }
        }
    }

    // Attacks: jumps in the whole mix's level (live: the mix's punch passing 0.25). A jump is a rise of a
    // quarter over the loudest of the frames a window's width before, timed at its steepest frame. Live also
    // counts its kick and snare triggers; a real drum hit is such a jump anyway, while the offline drum and
    // onset detectors measure each band against its own peak and so, on a track without drums, read the
    // spectral change every note change makes: with them, legato would never fire.
    std::vector<double> jumps;
    {
        const int lag = math::framesFor(0.07, rate);
        int runStart = -1, steepest = -1;
        float steepestRise = 0.0f;
        for (int t = 0; t <= frames; ++t) {
            bool jump = false;
            if (t < frames && t - lag - 2 >= 0 && features.rms[t] > 2.0f * silenceOn) {
                float before = 0.0f;
                for (int d = t - lag - 2; d <= t - lag; ++d) before = std::max(before, features.rms[d]);
                jump = features.rms[t] > 1.25f * before;
            }
            if (jump) {
                const float rise = features.rms[t] - features.rms[t - 1];
                if (runStart < 0 || rise > steepestRise) steepest = t, steepestRise = rise;
                if (runStart < 0) runStart = t;
            } else if (runStart >= 0) {
                jumps.push_back(steepest / rate);
                runStart = -1;
            }
        }
    }
    const std::vector<double>& attacks = jumps;  // ascending already

    // Events at each note's first frame.
    out.interval.assign(nf, 0.0f);
    for (size_t i = 0; i < notes.size(); ++i) {
        const Note& k = notes[i];
        const double at = k.start / rate;
        const float strength = std::clamp(pitch.confidence[k.start], 0.05f, 1.0f);
        out.notes.push_back(at);
        out.noteStrength.push_back(strength);
        const bool first = i == 0 || notes[i - 1].run != k.run;
        if (!first) {
            const float step = std::clamp(static_cast<float>(k.note - notes[i - 1].note) / 12.0f, -1.0f, 1.0f);
            for (int t = k.start; t < frames; ++t) out.interval[t] = step;
            const auto near = std::lower_bound(attacks.begin(), attacks.end(), at - 0.08);
            if (near == attacks.end() || *near > at + 0.05) {
                out.legatos.push_back(at);
                out.legatoStrength.push_back(strength);
            }
        }
        double lastStrike = at;
        for (auto o = std::upper_bound(jumps.begin(), jumps.end(), at + 0.1); o != jumps.end() && *o < k.end / rate; ++o) {
            if (*o - lastStrike < 0.1) continue;
            const int f = math::frameAt(*o, rate, frames);
            out.reattacks.push_back(*o);
            out.reattackStrength.push_back(std::clamp(pitch.confidence[f], 0.05f, 1.0f));
            lastStrike = *o;
        }
    }

    // Continuous values.
    out.confidence.assign(pitch.confidence.begin(), pitch.confidence.begin() + frames);
    out.pitch.assign(nf, 0.0f);
    std::vector<float> pc(nf, kNaN);
    std::vector<double> height(nf, 0.0), even(nf, 0.0);
    for (int t = 0; t < frames; ++t) {
        if (!voiced[t]) continue;
        out.pitch[t] = midi[t];
        pc[t] = std::fmod(std::fmod(midi[t], 12.0f) + 12.0f, 12.0f) / 12.0f;
        height[t] = std::clamp((midi[t] - 36.0f) / 48.0f, 0.0f, 1.0f);
        even[t] = pitch.evenness[t];
    }
    fillHeld(pc, 0.0f);
    out.pitchClass = pc;
    auto heldMean = [&](const std::vector<double>& x, double seconds) {
        const std::vector<double> m = math::centredMeanWhere(x, voiced, math::framesFor(seconds, rate), static_cast<double>(kNaN));
        std::vector<float> v(m.begin(), m.end());
        fillHeld(v, 0.0f);
        return v;
    };
    out.pitchHeight = heldMean(height, 0.08);
    out.evenness = heldMean(even, 0.1);

    // Glide and vibrato, within notes only: across a note change the slope is a step, not a slide.
    std::vector<double> slope(nf, 0.0), wobble(nf, 0.0);
    const int centre = math::framesFor(0.15, rate);
    for (const Note& k : notes) {
        for (int t = k.start; t < k.end; ++t) {
            if (!voiced[t]) continue;
            if (t - 1 >= k.start && t + 1 < k.end && voiced[t - 1] && voiced[t + 1]) {
                const float d = std::fabs(midi[t + 1] - midi[t - 1]);
                if (d <= 4.0f) slope[t] = d * rate / 2.0;  // more than two semitones a frame is a jump
            }
            double sum = 0.0;
            int n = 0;
            for (int d = std::max(k.start, t - centre); d < std::min(k.end, t + centre + 1); ++d)
                if (voiced[d]) sum += midi[d], ++n;
            wobble[t] = std::fabs(midi[t] - sum / n) / 0.5;
        }
    }
    {
        const std::vector<double> g = math::centredMean(slope, math::framesFor(0.1, rate));
        const std::vector<double> v = math::centredMean(wobble, centre);
        out.glide.resize(nf);
        out.vibrato.resize(nf);
        for (int t = 0; t < frames; ++t) {
            out.glide[t] = std::clamp(static_cast<float>(g[t] / 24.0), 0.0f, 1.0f);
            out.vibrato[t] = std::clamp(static_cast<float>(v[t]), 0.0f, 1.0f);
        }
    }

    // Run density: note changes a second through a two-sided 1 s kernel (integral 1), over 12.
    out.runDensity.assign(nf, 0.0f);
    for (int t = 0; t < frames; ++t) {
        const double now = t / rate;
        double r = 0.0;
        for (auto it = std::lower_bound(out.notes.begin(), out.notes.end(), now - 8.0); it != out.notes.end() && *it < now + 8.0; ++it)
            r += 0.5 * std::exp(-std::fabs(now - *it));
        out.runDensity[t] = std::clamp(static_cast<float>(r / 12.0), 0.0f, 1.0f);
    }
    return out;
}

void sampleMelody(const MelodyTrack& m, double seconds, double dt, GeodeSoundFrame& out) {
    const int frames = static_cast<int>(m.pitch.size());
    if (frames <= 0) return;
    const int t = math::frameAt(seconds, m.frameRate, frames);
    out.pitch = m.pitch[t];
    out.pitchClass = m.pitchClass[t];
    out.pitchHeight = m.pitchHeight[t];
    out.pitchConfidence = m.confidence[t];
    out.interval = m.interval[t];
    out.glide = m.glide[t];
    out.vibrato = m.vibrato[t];
    out.evenness = m.evenness[t];
    out.runDensity = m.runDensity[t];
    const int note = math::firstIn(m.notes, seconds, dt);
    out.noteChange = note >= 0 ? m.noteStrength[note] : 0.0f;
    const int legato = math::firstIn(m.legatos, seconds, dt);
    out.legato = legato >= 0 ? m.legatoStrength[legato] : 0.0f;
    const int strike = math::firstIn(m.reattacks, seconds, dt);
    out.reattack = strike >= 0 ? m.reattackStrength[strike] : 0.0f;
}

}  // namespace geode::sound::offline
