// The offline analyser's second half (core/sound/offline/OfflineTrack, OfflineMelody, OfflineHarmony,
// OfflineEmotion; #464). Self-contained, in android.yml's native host tests, on sounds built here in code
// where the right answer is known exactly:
//   coverage   sampleTrack writes every GeodeSoundFrame field, Form's (#465, below this) included;
//              nothing it writes is NaN or infinite
//   melody     a legato line's notes are found where they start, with their pitch classes and intervals,
//              and read as legato; a detached line's are not, and its repeated note is a reattack; every
//              note change fires once at 24, 30 and 60 fps
//   harmony    a chord change fires harmonicChange on the change and moves chordHue; a cluster is tenser
//              than a triad; a C major scale over a C drone reads as a scale on C holding those seven notes
//   events     silence fires where the music stops and returned where it comes back; a drop after a
//              breakdown is a surge; one burst is one hard cut, at any frame rate
//   stereo     a left-only signal pans left, an out-of-phase one is wide, a mono one is narrow
//   waveform   a steady tone stands still from frame to frame
//   tuning     gain moves low/middle/high, speed scales the clocks; the clocks never run backwards
//   emotion    a major chord feels happier than a minor one; GEMS sums to 1
//   silence    no events, every field finite, the scale reads "no music"
//
// Build: g++ -std=c++20 -O2 -Wall -Wextra -Werror -Icore core/tests/offline_track_test.cpp core/sound/offline/*.cpp core/sound/Fft.cpp -o offline_track_test
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "sound/offline/OfflineEmotion.hpp"
#include "sound/offline/OfflineTrack.hpp"
#include "tests/analysis/MirEval.hpp"

namespace off = geode::sound::offline;
using geode::analysis::fMeasure;

namespace {

int failures = 0;

void check(bool ok, const std::string& what, double value = 0.0) {
    std::printf("%s %s (%.4f)\n", ok ? "ok  " : "FAIL", what.c_str(), value);
    if (!ok) ++failures;
}

constexpr int kRate = 44100;
constexpr double kTau = 2.0 * std::numbers::pi;

struct Lcg {
    uint32_t s = 0x13579BDFu;
    float next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
    }
};

double hzOf(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

struct Stereo {
    std::vector<float> l, r;
    explicit Stereo(double seconds) : l(static_cast<size_t>(seconds * kRate), 0.0f), r(l.size(), 0.0f) {}
    size_t size() const { return l.size(); }
    void add(size_t i, float v) {
        if (i >= l.size()) return;
        l[i] += v;
        r[i] += v;
    }
    std::vector<float> interleaved() const {
        std::vector<float> out(2 * l.size());
        for (size_t i = 0; i < l.size(); ++i) out[2 * i] = l[i], out[2 * i + 1] = r[i];
        return out;
    }
};

off::Track analyse(const Stereo& s) {
    const std::vector<float> pcm = s.interleaved();
    return off::analyseTrack(pcm.data(), s.size(), 2, kRate);
}

// A harmonic tone (six partials at 1/h) from `from` to `to` seconds, with short linear ramps.
void addNote(Stereo& s, double midi, double from, double to, float gain, double ramp = 0.005) {
    const double hz = hzOf(midi);
    const size_t a = static_cast<size_t>(from * kRate), b = std::min(s.size(), static_cast<size_t>(to * kRate));
    for (size_t i = a; i < b; ++i) {
        const double t = static_cast<double>(i - a) / kRate, left = static_cast<double>(b - i) / kRate;
        const double env = std::min({1.0, t / ramp, left / ramp});
        double v = 0.0;
        for (int h = 1; h <= 6; ++h) v += std::sin(kTau * hz * h * t) / h;
        s.add(i, static_cast<float>(gain * env * v));
    }
}

// A phase-continuous line: each note follows the last with no break in level, so nothing attacks.
void addLegato(Stereo& s, const std::vector<double>& midi, double from, double each, float gain) {
    double phase = 0.0;
    const size_t a = static_cast<size_t>(from * kRate);
    const size_t n = static_cast<size_t>(each * kRate * midi.size());
    for (size_t i = 0; i < n; ++i) {
        const double hz = hzOf(midi[std::min(midi.size() - 1, i / static_cast<size_t>(each * kRate))]);
        phase += kTau * hz / kRate;
        const double t = static_cast<double>(i) / kRate, left = static_cast<double>(n - i) / kRate;
        const double env = std::min({1.0, t / 0.01, left / 0.01});
        double v = 0.0;
        for (int h = 1; h <= 6; ++h) v += std::sin(h * phase) / h;
        s.add(a + i, static_cast<float>(gain * env * v));
    }
}

void addKick(Stereo& s, double at) {
    const size_t start = static_cast<size_t>(at * kRate);
    double phase = 0.0;
    for (int i = 0; i < kRate * 3 / 10; ++i) {
        const double t = static_cast<double>(i) / kRate;
        phase += kTau * (45.0 + 75.0 * std::exp(-t / 0.04)) / kRate;
        s.add(start + i, 0.6f * static_cast<float>(std::sin(phase) * std::exp(-t / 0.12)));
    }
}

void addSnare(Stereo& s, double at, Lcg& rng) {
    const size_t start = static_cast<size_t>(at * kRate);
    for (int i = 0; i < kRate / 5; ++i) {
        const double t = static_cast<double>(i) / kRate;
        s.add(start + i, static_cast<float>(0.3 * rng.next() * std::exp(-t / 0.05) + 0.35 * std::sin(kTau * 185.0 * t) * std::exp(-t / 0.03)));
    }
}

void addNoise(Stereo& s, double from, double to, float gain, Lcg& rng) {
    for (size_t i = static_cast<size_t>(from * kRate); i < std::min(s.size(), static_cast<size_t>(to * kRate)); ++i) s.add(i, gain * rng.next());
}

float* fieldsOf(GeodeSoundFrame& f) { return reinterpret_cast<float*>(&f); }

bool allFinite(const GeodeSoundFrame& f) {
    const float* v = reinterpret_cast<const float*>(&f);
    for (int i = 0; i < GEODE_SOUND_FRAME_FLOATS; ++i)
        if (!std::isfinite(v[i])) return false;
    return true;
}

// How many frames fire a trigger read by `field` over the clip at `fps`.
template <typename Field>
int firings(const off::Track& track, double seconds, int fps, Field field) {
    GeodeSoundFrame f{};
    int n = 0;
    for (int k = 1; k <= static_cast<int>(seconds * fps); ++k) {
        off::sampleTrack(track, static_cast<double>(k) / fps, 1.0 / fps, f);
        n += field(f) > 0.0f;
    }
    return n;
}

GeodeSoundFrame at(const off::Track& track, double seconds) {
    GeodeSoundFrame f{};
    off::sampleTrack(track, seconds, 1.0 / 60, f);
    return f;
}

bool nearAny(const std::vector<double>& events, double t, double window) {
    return std::any_of(events.begin(), events.end(), [&](double e) { return std::fabs(e - t) <= window; });
}

void coverage() {
    // A band: an A major chord, a groove and a melody, panned a little so the stereo fields have work.
    Stereo s(16.0);
    addNote(s, 57, 0.0, 16.0, 0.08f);
    addNote(s, 61, 0.0, 16.0, 0.06f);
    addNote(s, 64, 0.0, 16.0, 0.06f);
    addLegato(s, {69, 71, 73, 76, 73, 71, 69, 68}, 2.0, 0.5, 0.1f);
    Lcg rng;
    for (double t = 0.5; t < 15.5; t += 0.5) {
        if (static_cast<int>(std::lround(t * 2)) % 2) addSnare(s, t, rng);
        else addKick(s, t);
    }
    for (size_t i = 0; i < s.size(); ++i) s.r[i] *= 0.8f;
    const off::Track track = analyse(s);

    GeodeSoundFrame probe;
    float* v = fieldsOf(probe);
    for (int i = 0; i < GEODE_SOUND_FRAME_FLOATS; ++i) v[i] = std::numeric_limits<float>::quiet_NaN();
    off::sampleTrack(track, 5.0, 1.0 / 60, probe);
    int written = 0;
    for (int i = 0; i < GEODE_SOUND_SCALARS; ++i) {
        written += !std::isnan(v[i]);
        if (std::isnan(v[i])) std::printf("     scalar %d (offset %zu) is not written\n", i, static_cast<size_t>(i) * sizeof(float));
    }
    check(written == GEODE_SOUND_SCALARS, "coverage: every scalar is written, Form's included", written);
    bool arrays = true;
    for (int i = GEODE_SOUND_SCALARS; i < GEODE_SOUND_FRAME_FLOATS; ++i) arrays = arrays && !std::isnan(v[i]);
    check(arrays, "coverage: eq, spectrum, waveform and the three chromas are written");

    bool finite = true;
    for (int k = 0; k <= 16 * 30; ++k) {
        GeodeSoundFrame f{};
        off::sampleTrack(track, k / 30.0, 1.0 / 30, f);
        finite = finite && allFinite(f);
    }
    check(finite, "coverage: every field finite over the whole clip at 30 fps");

    const off::EmotionTrack emotion = off::analyseEmotion(track);
    bool feelings = !emotion.frames.empty();
    for (double t = 0.0; t < 16.0 && feelings; t += 0.25) {
        const auto e = off::sampleEmotion(emotion, t, 1.0 / 60);
        float sum = 0.0f;
        for (float g : e.gems) sum += g;
        feelings = std::isfinite(e.valence) && std::isfinite(e.energy) && std::isfinite(e.tension) && std::fabs(sum - 1.0f) < 1e-3f;
    }
    check(feelings, "emotion: finite axes and GEMS summing to 1 along the clip");

    // The clocks never run backwards, and hitTime counts the hits.
    bool forward = true;
    float lastBass = 0.0f, lastHit = 0.0f, lastCurved = 0.0f;
    for (int k = 1; k <= 16 * 60; ++k) {
        const GeodeSoundFrame f = at(track, k / 60.0);
        forward = forward && f.bassTime >= lastBass && f.hitTime >= lastHit - 1e-4f && f.curvedTime >= lastCurved;
        lastBass = f.bassTime, lastHit = f.hitTime, lastCurved = f.curvedTime;
    }
    check(forward, "clocks: bassTime, curvedTime and hitTime only move forward");
    check(std::fabs(lastHit - static_cast<float>(track.hits.size())) <= 1.0f, "clocks: hitTime ends at the number of kicks and snares",
          lastHit - static_cast<double>(track.hits.size()));

    // Tuning: gain moves the shaped bands, speed scales the clocks.
    off::Track tuned = track;
    GeodeSoundTuning t = off::offlineDefaultTuning();
    t.gain[0] = 0.5f;
    t.speed = 2.0f;
    off::applyTuning(tuned, t);
    double lowDefault = 0.0, lowHalf = 0.0;
    for (int k = 0; k < track.frames; ++k) lowDefault += track.shaped[0][k], lowHalf += tuned.shaped[0][k];
    check(lowHalf < lowDefault, "tuning: half the gain lowers `low`", lowHalf / (lowDefault + 1e-9));
    const double ratio = at(tuned, 10.0).bassTime / std::max(1e-6f, at(track, 10.0).bassTime);
    check(std::fabs(ratio - 2.0) < 0.01, "tuning: twice the speed, twice the band clock", ratio);
}

void melody() {
    // A legato line from 1 s, 0.4 s a note: C4 E4 G4 E4 D4 C4 A3 C4.
    const std::vector<double> line = {60, 64, 67, 64, 62, 60, 57, 60};
    Stereo s(6.0);
    addLegato(s, line, 1.0, 0.4, 0.2f);
    const off::Track track = analyse(s);
    std::vector<double> truth;
    for (size_t i = 0; i < line.size(); ++i) truth.push_back(1.0 + 0.4 * i);
    const double f = fMeasure(truth, track.melody.notes, 0.05);
    check(f >= 0.9, "melody: the legato line's notes start where they start (F, 50 ms)", f);
    check(track.melody.legatos.size() + 1 >= line.size() - 1, "melody: the line's changes read as legato",
          static_cast<double>(track.melody.legatos.size()));
    const GeodeSoundFrame e = at(track, 1.6);  // inside the E4
    check(std::fabs(e.pitchClass - 4.0f / 12.0f) < 0.02f, "melody: E4 reads pitch class E", e.pitchClass * 12.0);
    check(std::fabs(e.pitch - 64.0f) < 0.5f, "melody: E4 reads MIDI 64", e.pitch);
    check(std::fabs(e.interval - 4.0f / 12.0f) < 1e-4f, "melody: C4 to E4 is a major third up", e.interval * 12.0);
    check(std::fabs(at(track, 2.0).interval - 3.0f / 12.0f) < 1e-4f, "melody: E4 to G4 is a minor third up", at(track, 2.0).interval * 12.0);
    check(at(track, 5.5).pitch == 0.0f && std::fabs(at(track, 5.5).pitchClass - 0.0f) < 0.02f,
          "melody: after the line, pitch is 0 and the class holds the last note's (C)", at(track, 5.5).pitchClass * 12.0);
    for (int fps : {24, 30, 60}) {
        const int n = firings(track, 6.0, fps, [](const GeodeSoundFrame& g) { return g.noteChange; });
        check(n == static_cast<int>(track.melody.notes.size()), "melody: every note change fires once at " + std::to_string(fps) + " fps", n);
    }

    // A detached line with 80 ms breaks: C4 D4 E4 E4 F4. Four notes (the repeated E is struck again), no legato.
    Stereo d(5.0);
    const std::vector<double> notes = {60, 62, 64, 64, 65};
    for (size_t i = 0; i < notes.size(); ++i) addNote(d, notes[i], 1.0 + 0.5 * i, 1.0 + 0.5 * i + 0.42, 0.2f);
    const off::Track dt = analyse(d);
    check(dt.melody.notes.size() == 4, "melody: the detached line has four notes", static_cast<double>(dt.melody.notes.size()));
    check(dt.melody.legatos.size() <= 1, "melody: struck notes are not legato", static_cast<double>(dt.melody.legatos.size()));
    check(nearAny(dt.melody.reattacks, 2.5, 0.06), "melody: the repeated E is a reattack", static_cast<double>(dt.melody.reattacks.size()));
}

void harmony() {
    // A major, D major, E major: four seconds each.
    Stereo s(12.0);
    const double chords[3][3] = {{57, 61, 64}, {62, 66, 69}, {64, 68, 71}};
    for (int c = 0; c < 3; ++c)
        for (double m : chords[c]) addNote(s, m, 4.0 * c, 4.0 * c + 4.0, 0.08f, 0.01);
    const off::Track track = analyse(s);
    const auto& changes = track.harmony.changes;
    check(nearAny(changes, 4.0, 0.3) && nearAny(changes, 8.0, 0.3), "harmony: both chord changes fire harmonicChange (±0.3 s)",
          static_cast<double>(changes.size()));
    check(changes.size() <= 3, "harmony: and little else", static_cast<double>(changes.size()));
    const float a = at(track, 2.0).chordHue, dHue = at(track, 6.0).chordHue;
    const float apart = std::min(std::fabs(a - dHue), 1.0f - std::fabs(a - dHue));
    check(apart > 0.03f, "harmony: chordHue moves from A major to D major", apart);
    check(firings(track, 12.0, 30, [](const GeodeSoundFrame& g) { return g.harmonicChange; }) == static_cast<int>(changes.size()),
          "harmony: each change fires once at 30 fps");

    // A cluster (C, C#, D) against a C major triad.
    Stereo cluster(4.0), triad(4.0);
    for (double m : {60.0, 61.0, 62.0}) addNote(cluster, m, 0.0, 4.0, 0.08f, 0.01);
    for (double m : {60.0, 64.0, 67.0}) addNote(triad, m, 0.0, 4.0, 0.08f, 0.01);
    const float tc = at(analyse(cluster), 2.0).tension, tt = at(analyse(triad), 2.0).tension;
    check(tc > tt + 0.2f, "harmony: a cluster is tenser than a triad", tc - tt);

    // A C major scale over a C drone: a scale rooted on C holding all seven notes.
    Stereo scale(10.0);
    addNote(scale, 48, 0.0, 10.0, 0.08f, 0.01);
    const double steps[8] = {60, 62, 64, 65, 67, 69, 71, 72};
    for (int k = 0; k < 40; ++k) addNote(scale, steps[k % 8], 0.25 * k, 0.25 * k + 0.24, 0.08f, 0.01);
    const off::Track st = analyse(scale);
    const GeodeSoundFrame mid = at(st, 5.0);
    const int index = static_cast<int>(mid.scale);
    const unsigned mask = index >= 0 ? off::harmonyScaleMask(index) : 0u;
    const unsigned major = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 11);
    check(index >= 0 && std::lround(mid.scaleRoot * 12.0f) == 0 && (mask & major) == major,
          "harmony: the scale is rooted on C and holds C D E F G A B", index);
    check(mid.scaleFit > 0.2f, "harmony: and fits clearly", mid.scaleFit);
    check(std::fabs(mid.scaleBrightness - (0.5f + 0.5f * off::harmonyScaleBrightness(index))) < 0.05f,
          "harmony: scaleBrightness is that scale's", mid.scaleBrightness);
}

void events() {
    Lcg rng;
    // Music, two seconds of silence, music.
    Stereo s(9.0);
    addNoise(s, 0.0, 4.0, 0.05f, rng);
    addNote(s, 57, 0.0, 4.0, 0.05f);
    addNoise(s, 6.0, 9.0, 0.05f, rng);
    addNote(s, 57, 6.0, 9.0, 0.05f);
    const off::Track track = analyse(s);
    check(track.silences.size() == 1 && nearAny(track.silences, 4.0, 0.1), "events: silence fires where the music stops",
          track.silences.empty() ? -1.0 : track.silences[0]);
    check(track.returns.size() == 1 && nearAny(track.returns, 6.0, 0.1), "events: returned fires where it comes back",
          track.returns.empty() ? -1.0 : track.returns[0]);
    check(at(track, 5.0).presence < 0.2f && at(track, 2.0).presence > 0.9f, "events: presence fades out over the silence", at(track, 5.0).presence);

    // Loud, a quiet breakdown, loud again: the return is a surge.
    Stereo b(16.0);
    addNoise(b, 0.0, 8.0, 0.2f, rng);
    addNoise(b, 8.0, 12.0, 0.05f, rng);
    addNoise(b, 12.0, 16.0, 0.2f, rng);
    const off::Track bt = analyse(b);
    check(nearAny(bt.surges, 12.0, 0.25), "events: the drop after the breakdown is a surge", bt.surges.empty() ? -1.0 : bt.surges[0]);
    check(bt.surges.size() == 1, "events: and the only one", static_cast<double>(bt.surges.size()));

    // Quiet noise with one loud burst at 5 s: one hard cut, on it, at every frame rate.
    Stereo h(8.0);
    addNoise(h, 0.0, 8.0, 0.01f, rng);
    addNoise(h, 5.0, 5.06, 0.6f, rng);
    const off::Track ht = analyse(h);
    check(ht.hardCuts.size() == 1 && nearAny(ht.hardCuts, 5.0, 0.15), "events: one burst, one hard cut on it",
          ht.hardCuts.empty() ? -1.0 : ht.hardCuts[0]);
    for (int fps : {24, 30, 60}) {
        const int n = firings(ht, 8.0, fps, [](const GeodeSoundFrame& g) { return g.hardCut; });
        check(n == static_cast<int>(ht.hardCuts.size()), "events: the hard cut fires once at " + std::to_string(fps) + " fps", n);
    }
}

void stereo() {
    Lcg rng;
    Stereo left(3.0), wide(3.0), mono(3.0);
    for (size_t i = 0; i < left.size(); ++i) {
        const float v = 0.2f * rng.next();
        left.l[i] = v;
        wide.l[i] = v, wide.r[i] = -v;
        mono.l[i] = v, mono.r[i] = v;
    }
    check(at(analyse(left), 1.5).stereoPan < -0.9f, "stereo: a left-only signal pans left", at(analyse(left), 1.5).stereoPan);
    check(at(analyse(wide), 1.5).stereoWidth > 0.9f, "stereo: an out-of-phase signal is wide", at(analyse(wide), 1.5).stereoWidth);
    check(at(analyse(mono), 1.5).stereoWidth < 0.05f, "stereo: a mono signal is narrow", at(analyse(mono), 1.5).stereoWidth);
}

void waveform() {
    // 441 Hz: a period of exactly 100 samples, so the drawn wave must not crawl from frame to frame.
    Stereo s(3.0);
    for (size_t i = 0; i < s.size(); ++i) s.add(i, 0.5f * static_cast<float>(std::sin(kTau * 441.0 * i / kRate)));
    const off::Track track = analyse(s);
    const GeodeSoundFrame a = at(track, 1.0), b = at(track, 1.0 + 1.0 / 60);
    double diff = 0.0, peak = 0.0;
    for (int i = 0; i < GEODE_SOUND_WAVEFORM_POINTS; ++i) {
        diff += std::fabs(a.waveform[i] - b.waveform[i]) / GEODE_SOUND_WAVEFORM_POINTS;
        peak = std::max(peak, static_cast<double>(std::fabs(a.waveform[i])));
    }
    check(diff < 0.02, "waveform: a steady tone stands still", diff);
    check(peak > 0.4 && peak < 0.55, "waveform: at its own amplitude", peak);
}

void emotion() {
    auto feeling = [](const std::array<double, 3>& chord) {
        Stereo s(12.0);
        for (double m : chord) addNote(s, m, 0.0, 12.0, 0.08f, 0.01);
        const off::Track track = off::analyseTrack(s.interleaved().data(), s.size(), 2, kRate);
        const off::EmotionTrack e = off::analyseEmotion(track);
        double v = 0.0;
        for (double t = 3.0; t < 9.0; t += 0.5) v += off::sampleEmotion(e, t, 1.0 / 60).valence / 12.0;
        return v;
    };
    const double major = feeling({57, 61, 64}), minor = feeling({57, 60, 64});
    check(major > minor + 0.1, "emotion: A major feels happier than A minor", major - minor);
}

void silence() {
    const std::vector<float> zeros(2 * 6 * kRate, 0.0f);
    const off::Track track = off::analyseTrack(zeros.data(), 6 * kRate, 2, kRate);
    check(track.hardCuts.empty() && track.silences.empty() && track.returns.empty() && track.surges.empty() && track.melody.notes.empty() &&
              track.harmony.changes.empty(),
          "silence: no events");
    const GeodeSoundFrame f = at(track, 3.0);
    check(allFinite(f), "silence: every field finite");
    check(f.scale == -1.0f && f.scaleBrightness == 0.5f && f.scaleFit == 0.0f, "silence: the scale reads no music", f.scale);
}

}  // namespace

int main() {
    coverage();
    melody();
    harmony();
    events();
    stereo();
    waveform();
    emotion();
    silence();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("offline track: all checks passed\n");
    return 0;
}
