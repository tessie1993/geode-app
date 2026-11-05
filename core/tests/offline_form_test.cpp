// The offline analyser's form (core/sound/offline/OfflineForm, #465), in android.yml's native host tests, on
// songs built here in code whose sections are known by construction:
//
//   an EDM-shaped song (intro, verse, build, drop, outro): the boundaries land within half a second of the
//     bar lines the score put them on, the one drop lands on the drop's first beat with its build before
//     it, and each section reads the state live Form would give it (intro, verse, build, drop, outro)
//   a verse-chorus song (A B A B): the returning sections get their old sectionId back
//   a steady four-chord loop: no boundary at all (the box kernel sees the same loop on both sides)
//   silence: no sections, and sample() still writes a quiet frame
//   determinism: the same samples give the same timeline
//   sample() at 24, 30 and 60 fps: every boundary and drop fires once, at the frame that holds it, and the
//     section, state and phrase fields agree at the song times all three share (spec #455 story 33)
//
// The thresholds leave room for the beat grid's own error (OfflineBeats, #463): a boundary is a beat time.
//
// Build: g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/offline_form_test.cpp \
//          core/sound/offline/OfflineForm.cpp core/sound/offline/OfflineBeats.cpp core/sound/Fft.cpp \
//          -o offline_form_test
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include "sound/offline/OfflineBeats.hpp"
#include "sound/offline/OfflineForm.hpp"
#include "tests/analysis/MirEval.hpp"

namespace off = geode::sound::offline;
using namespace geode::analysis;

namespace {

int failures = 0;

void check(bool ok, const char* what, double value = 0.0) {
    std::printf("%s %s (%.4f)\n", ok ? "ok  " : "FAIL", what, value);
    if (!ok) ++failures;
}

constexpr int kRate = 44100;
constexpr double kTau = 2.0 * std::numbers::pi;
constexpr double kLead = 0.25;

struct Lcg {
    uint32_t s = 0x2468ace1u;
    float next() {  // -1..1
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
    }
};

int at(double seconds) { return static_cast<int>(std::lround(seconds * kRate)); }

void addKick(std::vector<float>& y, double t0, float gain) {
    double phase = 0.0;
    for (int i = 0, s = at(t0); i < kRate * 35 / 100 && s + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        phase += kTau * (45.0 + 75.0 * std::exp(-t / 0.04)) / kRate;
        y[s + i] += gain * static_cast<float>(std::sin(phase) * std::exp(-t / 0.12));
    }
}

void addSnare(std::vector<float>& y, double t0, float gain, Lcg& rng) {
    for (int i = 0, s = at(t0); i < kRate / 5 && s + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        y[s + i] += gain * static_cast<float>(0.4 * rng.next() * std::exp(-t / 0.05) +
                                              0.5 * std::sin(kTau * 185.0 * t) * std::exp(-t / 0.03));
    }
}

void addHat(std::vector<float>& y, double t0, float gain, Lcg& rng) {
    float prev = 0.0f;
    for (int i = 0, s = at(t0); i < kRate / 20 && s + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        const float white = rng.next();
        y[s + i] += gain * static_cast<float>((white - prev) * std::exp(-t / 0.015));
        prev = white;
    }
}

// A tone with a few harmonics, a short attack and release: pads (a chord) and bass (one low note).
void addTone(std::vector<float>& y, double t0, double seconds, const std::vector<double>& hz, float gain) {
    const int len = static_cast<int>(seconds * kRate);
    for (int i = 0, s = at(t0); i < len && s + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double env = std::min(1.0, t / 0.02) * std::min(1.0, (seconds - t) / 0.05);
        double v = 0.0;
        for (double f : hz) v += std::sin(kTau * f * t) + 0.3 * std::sin(kTau * 2.0 * f * t) + 0.1 * std::sin(kTau * 3.0 * f * t);
        y[s + i] += gain * static_cast<float>(env * v / hz.size());
    }
}

// Rising noise over a stretch: the riser of a build, brightening as it climbs (a first difference mixed in).
void addRiser(std::vector<float>& y, double t0, double seconds, float gain, Lcg& rng) {
    const int len = static_cast<int>(seconds * kRate);
    float prev = 0.0f;
    for (int i = 0, s = at(t0); i < len && s + i < static_cast<int>(y.size()); ++i) {
        const double x = static_cast<double>(i) / len;
        const float white = rng.next();
        const double bright = (white - prev) * x + white * (1.0 - x) * 0.3;
        y[s + i] += gain * static_cast<float>(x * x * bright);
        prev = white;
    }
}

double midiHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

std::vector<double> triad(int root, bool minor, int octaveBase) {
    return {midiHz(octaveBase + root), midiHz(octaveBase + root + (minor ? 3 : 4)), midiHz(octaveBase + root + 7)};
}

struct Chord {
    int root;
    bool minor;
};

// One section of a score: what plays in every bar of it.
struct Part {
    int bars = 8;
    std::vector<Chord> chords;  // one a bar, cycling
    float gain = 1.0f;
    bool pad = true, brightPad = false;
    std::vector<double> kick, snare;  // beats in the bar
    int hats = 0;                     // 0, 8 or 16 a bar
    int bassPerBar = 0;               // 0, 2 (on 1 and 3) or 8 (eighths)
    bool roll = false, riser = false;
};

struct Song {
    std::vector<float> mono;
    std::vector<double> boundaries;     // each section's start after the first
    std::vector<double> sectionStarts;  // every section's start, then the end of the last
    double bar = 0.0;
};

Song build(double bpm, const std::vector<Part>& parts) {
    Song song;
    const double beat = 60.0 / bpm;
    song.bar = 4 * beat;
    int bars = 0;
    for (const Part& p : parts) bars += p.bars;
    song.mono.assign(static_cast<size_t>((kLead + bars * song.bar + 1.0) * kRate), 0.0f);
    Lcg rng;
    int barIndex = 0;
    for (size_t s = 0; s < parts.size(); ++s) {
        const Part& p = parts[s];
        const double start = kLead + barIndex * song.bar;
        song.sectionStarts.push_back(start);
        if (s > 0) song.boundaries.push_back(start);
        for (int b = 0; b < p.bars; ++b) {
            const double t = start + b * song.bar;
            const Chord c = p.chords[static_cast<size_t>(b) % p.chords.size()];
            const float g = p.gain;
            if (p.pad) {
                std::vector<double> notes = triad(c.root, c.minor, 60);
                if (p.brightPad)
                    for (double f : triad(c.root, c.minor, 72)) notes.push_back(f);
                addTone(song.mono, t, song.bar, notes, 0.35f * g);
            }
            for (double k : p.kick) addKick(song.mono, t + k * beat, 0.8f * g);
            for (double k : p.snare) addSnare(song.mono, t + k * beat, 0.5f * g, rng);
            for (int k = 0; k < p.hats; ++k) addHat(song.mono, t + k * song.bar / p.hats, (k % (p.hats / 4) == 0 ? 0.3f : 0.18f) * g, rng);
            if (p.bassPerBar > 0) {
                const double len = song.bar / p.bassPerBar * 0.9;
                for (int k = 0; k < p.bassPerBar; ++k)
                    addTone(song.mono, t + k * song.bar / p.bassPerBar, len, {midiHz(36 + c.root)}, 0.6f * g);
            }
        }
        if (p.roll) {  // a snare roll that speeds up and swells through the section
            const double length = p.bars * song.bar;
            double t = 0.0, step = beat / 2;
            while (t < length - 1e-9) {
                addSnare(song.mono, start + t, static_cast<float>((0.15 + 0.6 * t / length) * p.gain), rng);
                t += step;
                step = std::max(beat / 8, step * 0.93);
            }
        }
        if (p.riser) addRiser(song.mono, start, p.bars * song.bar, 0.5f * p.gain, rng);
        barIndex += p.bars;
    }
    song.sectionStarts.push_back(kLead + bars * song.bar);
    return song;
}

GeodeSoundFrame frameAt(const off::FormTimeline& form, const off::BeatGrid& grid, double t, double dt = 1.0 / 60) {
    GeodeSoundFrame f{};
    off::sample(grid, t, dt, f);
    off::sample(form, grid, t, dt, f);
    return f;
}

// A point well inside section s: three quarters of the way through, clear of a boundary a beat off.
double inside(const Song& song, size_t s) {
    return song.sectionStarts[s] + 0.75 * (song.sectionStarts[s + 1] - song.sectionStarts[s]);
}

const std::vector<Chord> kMinorLoop = {{9, true}, {2, true}, {4, false}, {9, true}};  // Am Dm E Am
const std::vector<Chord> kVerseLoop = {{0, false}, {7, false}, {9, true}, {5, false}};  // C G Am F
const std::vector<Chord> kChorusLoop = {{5, false}, {7, false}, {4, true}, {9, true}};  // F G Em Am

// A pad alone; a pop groove (kick on 1 and 3, snare on 2 and 4, eighth hats, bass on 1 and 3); a full
// chorus or drop (four on the floor, sixteenth hats, eighth bass, the pad an octave brighter).
Part part(int bars, const std::vector<Chord>& chords, float gain) {
    Part p;
    p.bars = bars;
    p.chords = chords;
    p.gain = gain;
    return p;
}

Part groove(int bars, const std::vector<Chord>& chords, float gain) {
    Part p = part(bars, chords, gain);
    p.kick = {0, 2};
    p.snare = {1, 3};
    p.hats = 8;
    p.bassPerBar = 2;
    return p;
}

Part chorus(int bars, const std::vector<Chord>& chords, float gain) {
    Part p = part(bars, chords, gain);
    p.brightPad = true;
    p.kick = {0, 1, 2, 3};
    p.snare = {1, 3};
    p.hats = 16;
    p.bassPerBar = 8;
    return p;
}

void edmSong() {
    Part intro = part(8, kMinorLoop, 0.5f);
    intro.hats = 8;
    const Part verse = groove(8, kMinorLoop, 0.85f);
    Part buildUp = part(4, kMinorLoop, 0.7f);
    buildUp.roll = buildUp.riser = true;
    const Part drop = chorus(8, kMinorLoop, 1.0f);
    const Part outro = part(4, kMinorLoop, 0.4f);
    const Song song = build(124.0, {intro, verse, buildUp, drop, outro});
    const off::BeatGrid grid = off::analyse(song.mono.data(), song.mono.size(), kRate);
    const off::FormTimeline form = off::analyseForm(song.mono.data(), song.mono.size(), kRate, grid);

    const double f05 = boundaryF(song.boundaries, form.boundaries, 0.5);
    const double f3 = boundaryF(song.boundaries, form.boundaries, 3.0);
    check(f05 >= 0.75, "edm: boundary F at 0.5 s >= 0.75", f05);
    check(f3 >= 0.85, "edm: boundary F at 3 s >= 0.85", f3);

    const double dropTruth = song.sectionStarts[3];
    check(form.drops.size() == 1, "edm: one drop", static_cast<double>(form.drops.size()));
    if (!form.drops.empty()) {
        const off::FormDrop& d = form.drops.front();
        check(std::fabs(d.at - dropTruth) <= 0.5, "edm: the drop lands on its first beat (within 0.5 s)", d.at - dropTruth);
        check(std::fabs(d.buildStart - song.sectionStarts[2]) <= song.bar, "edm: its build starts with the build section",
              d.buildStart - song.sectionStarts[2]);
        check(d.strength >= off::kFormEventFloor && d.strength <= 1.0f, "edm: drop strength in kEventFloor..1", d.strength);
        check(std::any_of(form.boundaries.begin(), form.boundaries.end(), [&](double b) { return b == d.at; }),
              "edm: the drop is also a boundary");
    }

    const int expected[5] = {GEODE_SECTION_INTRO, GEODE_SECTION_VERSE, GEODE_SECTION_BUILD, GEODE_SECTION_DROP, GEODE_SECTION_OUTRO};
    const char* names[5] = {"edm: intro reads INTRO", "edm: verse reads VERSE", "edm: build reads BUILD", "edm: drop reads DROP",
                            "edm: outro reads OUTRO"};
    for (size_t s = 0; s < 5; ++s) {
        const GeodeSoundFrame f = frameAt(form, grid, inside(song, s));
        check(static_cast<int>(f.sectionState) == expected[s], names[s], f.sectionState);
    }
    const GeodeSoundFrame verseF = frameAt(form, grid, inside(song, 1));
    const GeodeSoundFrame buildF = frameAt(form, grid, inside(song, 2));
    const GeodeSoundFrame dropF = frameAt(form, grid, inside(song, 3));
    check(verseF.buildUp == 0.0f && dropF.buildUp == 0.0f, "edm: no buildUp outside the build");
    check(buildF.buildUp > 0.5f && buildF.buildUp < 1.0f, "edm: buildUp climbing late in the build", buildF.buildUp);

    // Determinism, on the richest song.
    const off::FormTimeline again = off::analyseForm(song.mono.data(), song.mono.size(), kRate, grid);
    bool same = again.boundaries == form.boundaries && again.novelty == form.novelty && again.drops.size() == form.drops.size() &&
                again.states.size() == form.states.size();
    for (size_t i = 0; same && i < form.drops.size(); ++i) same = again.drops[i].at == form.drops[i].at;
    check(same, "edm: deterministic");

    // sample() at three export frame rates.
    const double duration = static_cast<double>(song.mono.size()) / kRate;
    std::vector<std::vector<float>> shared;
    for (int fps : {24, 30, 60}) {
        int changes = 0, drops = 0;
        bool onTime = true, phraseAtSections = true;
        std::vector<float> values;
        const int frames = static_cast<int>(std::floor(duration * 6.0)) * (fps / 6);
        for (int k = 1; k <= frames; ++k) {
            const double t = static_cast<double>(k) / fps, dt = 1.0 / fps;
            const GeodeSoundFrame f = frameAt(form, grid, t, dt);
            if (f.sectionChange > 0.0f) {
                ++changes;
                onTime = onTime && std::any_of(form.boundaries.begin(), form.boundaries.end(),
                                               [&](double b) { return b > t - dt && b <= t; });
                // A section that starts on a bar line starts a phrase there, as live's do.
                const bool onDownbeat = std::any_of(grid.downbeats.begin(), grid.downbeats.end(),
                                                    [&](double d) { return d > t - dt && d <= t; });
                if (onDownbeat) phraseAtSections = phraseAtSections && f.phrase > 0.0f;
                if (f.sectionChange < off::kFormEventFloor) onTime = false;
            }
            if (f.drop > 0.0f) ++drops;
            if (k % (fps / 6) == 0) {
                values.push_back(f.sectionId);
                values.push_back(f.sectionState);
                values.push_back(f.sectionBars);
                values.push_back(f.phraseIndex);
            }
        }
        check(changes == static_cast<int>(form.boundaries.size()), "sample: every boundary fires once", changes);
        check(drops == static_cast<int>(form.drops.size()), "sample: every drop fires once", drops);
        check(onTime, "sample: each sectionChange on the frame that holds its boundary, at kEventFloor or more");
        check(phraseAtSections, "sample: a section on a bar line starts a phrase");
        shared.push_back(values);
    }
    bool agree = shared[0] == shared[1] && shared[1] == shared[2];
    check(agree, "sample: 24, 30 and 60 fps agree at shared times");
}

void verseChorus() {
    const Part a = groove(8, kVerseLoop, 0.7f);
    const Part b = chorus(8, kChorusLoop, 0.9f);
    const Song song = build(110.0, {a, b, a, b});
    const off::BeatGrid grid = off::analyse(song.mono.data(), song.mono.size(), kRate);
    const off::FormTimeline form = off::analyseForm(song.mono.data(), song.mono.size(), kRate, grid);
    const double f3 = boundaryF(song.boundaries, form.boundaries, 3.0);
    check(f3 >= 0.8, "verse-chorus: boundary F at 3 s >= 0.8", f3);
    check(form.drops.empty(), "verse-chorus: no drop (the bass never left)", static_cast<double>(form.drops.size()));
    float id[4];
    for (size_t s = 0; s < 4; ++s) id[s] = frameAt(form, grid, inside(song, s)).sectionId;
    check(id[0] == id[2], "verse-chorus: the verse returns with its sectionId", id[2]);
    check(id[1] == id[3], "verse-chorus: the chorus returns with its sectionId", id[3]);
    check(id[0] != id[1], "verse-chorus: verse and chorus differ", id[1]);
}

void steadyLoop() {
    const Song song = build(96.0, {groove(16, kVerseLoop, 0.8f)});
    const off::BeatGrid grid = off::analyse(song.mono.data(), song.mono.size(), kRate);
    const off::FormTimeline form = off::analyseForm(song.mono.data(), song.mono.size(), kRate, grid);
    check(form.boundaries.empty(), "loop: a four-chord loop has no boundary", static_cast<double>(form.boundaries.size()));
    check(form.drops.empty(), "loop: and no drop");
    const GeodeSoundFrame f = frameAt(form, grid, inside(song, 0));
    check(static_cast<int>(f.sectionState) == GEODE_SECTION_VERSE, "loop: a steady loop is a verse", f.sectionState);
}

void silence() {
    const std::vector<float> quiet(kRate * 10, 0.0f);
    const off::BeatGrid grid = off::analyse(quiet.data(), quiet.size(), kRate);
    const off::FormTimeline form = off::analyseForm(quiet.data(), quiet.size(), kRate, grid);
    check(form.sections.empty() && form.boundaries.empty() && form.drops.empty(), "silence: no sections");
    GeodeSoundFrame f{};
    f.sectionChange = f.drop = f.buildUp = 1.0f;
    off::sample(form, grid, 5.0, 1.0 / 60, f);
    check(f.sectionChange == 0.0f && f.drop == 0.0f && f.buildUp == 0.0f && f.novelty == 0.0f &&
              static_cast<int>(f.sectionState) == GEODE_SECTION_INTRO,
          "silence: sample writes a quiet frame");
    const off::FormTimeline none = off::analyseForm(nullptr, 0, kRate, grid);
    check(none.sections.empty(), "no samples: no sections");
}

}  // namespace

int main() {
    edmSong();
    verseChorus();
    steadyLoop();
    silence();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("offline form: all checks passed\n");
    return 0;
}
