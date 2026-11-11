// A small golden vector for the sound engine (core/sound): a fixed 9 s synthetic song in, the whole
// GeodeSoundFrame (every scalar and every array) at six instants out, pinned to core/tests/sound_golden.txt.
//
//   sound_golden_test core/tests/sound_golden.txt           checks every value against the fixture
//   sound_golden_test --write core/tests/sound_golden.txt   rewrites it after a deliberate change to the analysis
//
// Why: sound_rate_test proves the engine hears the same at every frame rate, not that it hears what it
// heard yesterday. A refactor that should change nothing (moving the hop loop out of SoundEngine, a
// schema-generated frame) shows here at once. A change meant to alter the analysis rewrites the fixture
// in the same commit, and the diff names every value that moved.
//
// Six instants, not every frame: the deleted 885 KB golden pinned 1200 frames and its diffs said
// nothing. Deterministic across compilers and -O levels: the song is built in double from a fixed LCG,
// and values compare within a tolerance (1e-4 absolute plus 1e-4 relative), never bit for bit, so a libm
// that rounds sin or exp one ulp differently passes; a changed detector moves values by far more.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "sound/SoundEngine.hpp"
#include "tests/SoundFields.hpp"

using geode::sound::SoundEngine;
using geode::sound::test::kFieldCount;
using geode::sound::test::kFields;

namespace {

constexpr int kRate = 48000;
constexpr double kFps = 60.0;
constexpr double kSeconds = 9.0;
constexpr double kTau = 2.0 * std::numbers::pi;
// The frames pinned, by index at 60 fps: 0.5 s (warming up), 2, 3.5 (the tempo's first estimate),
// 5, 7 (D minor, after the key change) and 9 s (a second into the silence).
constexpr int kPinned[] = {30, 120, 210, 300, 420, 540};

struct Lcg {
    uint32_t state = 0x5EED6E0Du;
    double next() {  // -1 .. 1
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 8388608.0 - 1.0;
    }
};

double midiHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

// 103 BPM: kick on 1 and 3, snare on 2 and 4, eighth hats, a bass on the chord root and a triad, one
// chord a bar. F major (F–Dm–Bb–C) to 5 s, D minor (Dm–Gm–A–Dm) to 8 s, then silence.
std::vector<float> buildSong() {
    const int64_t total = static_cast<int64_t>(kSeconds * kRate);
    std::vector<float> out(static_cast<size_t>(2 * total));
    constexpr double beat = 60.0 / 103.0;
    constexpr int kMajorRoots[4] = {5, 2, 10, 0};
    constexpr bool kMajorMinor[4] = {false, true, false, false};
    constexpr int kMinorRoots[4] = {2, 7, 9, 2};
    constexpr bool kMinorMinor[4] = {true, true, false, true};
    Lcg noise;
    double prev = 0.0;
    for (int64_t s = 0; s < total; s++) {
        const double t = static_cast<double>(s) / kRate;
        const double n = noise.next();
        const double hatNoise = n - prev;
        prev = n;
        if (t >= 8.0) continue;
        const bool second = t >= 5.0;
        const int bar = static_cast<int>(t / (4 * beat)) % 4;
        const int root = second ? kMinorRoots[bar] : kMajorRoots[bar];
        const bool minor = second ? kMinorMinor[bar] : kMajorMinor[bar];
        double l = 0.0;
        double r = 0.0;
        const double sinceKick = std::fmod(t, 2 * beat);
        const double kick = 0.8 * std::sin(kTau * (48.0 * sinceKick + 70.0 * 0.03 * (1.0 - std::exp(-sinceKick / 0.03)))) *
                            std::exp(-sinceKick / 0.15);
        l += kick;
        r += kick;
        if (t >= beat) {
            const double since = std::fmod(t - beat, 2 * beat);
            const double sn = 0.25 * std::sin(kTau * 185.0 * since) * std::exp(-since / 0.05) + 0.3 * n * std::exp(-since / 0.08);
            l += sn;
            r += sn;
        }
        const double sinceHat = std::fmod(t, beat / 2);
        const double hat = 0.3 * hatNoise * std::exp(-sinceHat / 0.02);
        l += 0.7 * hat;
        r += hat;
        const double bassHz = midiHz(36 + root);
        double bass = 0.0;
        for (int h = 1; h <= 3; h++) bass += std::sin(kTau * bassHz * h * t) / h;
        const double sinceBass = std::fmod(t, beat);
        bass *= 0.2 * std::min(1.0, sinceBass / 0.005) * std::exp(-sinceBass / 0.25);
        l += bass;
        r += bass;
        const int notes[3] = {60 + root, 60 + root + (minor ? 3 : 4), 60 + root + 7};
        const double sinceBar = std::fmod(t, 4 * beat);
        const double env = std::min(1.0, sinceBar / 0.04) * (0.6 + 0.4 * std::exp(-sinceBar / 0.5));
        for (int i = 0; i < 3; i++) {
            const double hz = midiHz(notes[i]);
            const double v = std::sin(kTau * hz * t) + 0.4 * std::sin(kTau * 2 * hz * t) + 0.15 * std::sin(kTau * 3 * hz * t);
            l += 0.08 * env * v * (1.2 - 0.4 * i);
            r += 0.08 * env * v * (0.8 + 0.4 * i);
        }
        out[2 * s] = static_cast<float>(std::tanh(0.8 * l));
        out[2 * s + 1] = static_cast<float>(std::tanh(0.8 * r));
    }
    return out;
}

struct Value {
    int frame;  // at 60 fps; written as its time in seconds
    std::string name;
    double value;
};

void addArray(std::vector<Value>& into, int frame, const char* name, const float* v, int n) {
    for (int i = 0; i < n; i++) into.push_back({frame, std::string(name) + "[" + std::to_string(i) + "]", v[i]});
}

// Fed the way the export path feeds the engine: the audio up to each frame's end, then step(1/fps).
std::vector<Value> runSong() {
    const std::vector<float> song = buildSong();
    SoundEngine engine(kRate);
    std::vector<Value> pinned;
    const int64_t total = static_cast<int64_t>(song.size() / 2);
    int64_t done = 0;
    GeodeSoundFrame f{};
    size_t next = 0;
    for (int i = 1; next < std::size(kPinned); i++) {
        const int64_t end = std::min(total, static_cast<int64_t>(std::llround(i * kRate / kFps)));
        engine.write(song.data() + 2 * done, static_cast<int>(end - done), 2);
        done = end;
        engine.step(static_cast<float>(1.0 / kFps), f);
        if (i != kPinned[next]) continue;
        next++;
        for (int k = 0; k < kFieldCount; k++) pinned.push_back({i, kFields[k].name, f.*kFields[k].member});
        addArray(pinned, i, "eq", f.eq, GEODE_SOUND_EQ_BANDS);
        addArray(pinned, i, "spectrum", f.spectrum, GEODE_SOUND_SPECTRUM_POINTS);
        addArray(pinned, i, "waveform", f.waveform, GEODE_SOUND_WAVEFORM_POINTS);
        addArray(pinned, i, "chroma", f.chroma, GEODE_SOUND_CHROMA);
        addArray(pinned, i, "chromaFast", f.chromaFast, GEODE_SOUND_CHROMA);
        addArray(pinned, i, "chromaSlow", f.chromaSlow, GEODE_SOUND_CHROMA);
    }
    return pinned;
}

std::string text(const std::vector<Value>& values) {
    std::string out =
        "# sound_golden_test: the GeodeSoundFrame of a fixed synthetic song at six frames (60 fps).\n"
        "# Regenerate after a deliberate change to core/sound: sound_golden_test --write core/tests/sound_golden.txt\n"
        "# seconds field value\n";
    char buf[96];
    for (const Value& v : values) {
        std::snprintf(buf, sizeof buf, "%.3f %s %.9g\n", v.frame / kFps, v.name.c_str(), v.value);
        out += buf;
    }
    return out;
}

constexpr double kAbs = 1e-4;
constexpr double kRel = 1e-4;

int check(const std::vector<Value>& got, const char* path) {
    std::ifstream in(path);
    if (!in) {
        std::printf("FAIL cannot read %s\n", path);
        return 1;
    }
    std::vector<Value> want;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        Value v;
        double seconds = 0.0;
        row >> seconds >> v.name >> v.value;
        v.frame = static_cast<int>(std::lround(seconds * kFps));
        want.push_back(v);
    }
    int failures = 0;
    if (want.size() != got.size()) {
        std::printf("FAIL %s holds %zu values, the engine gave %zu: regenerate it with --write\n", path, want.size(), got.size());
        return 1;
    }
    double worst = 0.0;
    for (size_t i = 0; i < got.size(); i++) {
        const Value& w = want[i];
        const Value& g = got[i];
        if (w.frame != g.frame || w.name != g.name) {
            std::printf("FAIL line %zu: the fixture has %d %s, the engine %d %s\n", i + 1, w.frame, w.name.c_str(), g.frame, g.name.c_str());
            return 1;
        }
        const double diff = std::fabs(g.value - w.value);
        const bool same = std::isfinite(g.value) && diff <= kAbs + kRel * std::fabs(w.value);
        worst = std::max(worst, diff / (kAbs + kRel * std::fabs(w.value)));
        if (!same) {
            if (failures < 40) std::printf("FAIL frame %d (%.2f s) %-16s fixture %.9g  engine %.9g\n", g.frame, g.frame / kFps, g.name.c_str(), w.value, g.value);
            failures++;
        }
    }
    std::printf("%zu values at %zu frames, worst difference %.3g of the tolerance\n", got.size(), std::size(kPinned), worst);
    if (failures > 0) std::printf("FAIL %d values moved; if the change to core/sound was meant, rerun with --write and commit the fixture\n", failures);
    return failures;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<Value> values = runSong();
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << text(values);
        std::printf("wrote %zu values to %s\n", values.size(), argv[2]);
        return 0;
    }
    const int failures = check(values, argc > 1 ? argv[1] : "core/tests/sound_golden.txt");
    if (failures == 0) std::printf("sound_golden_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
