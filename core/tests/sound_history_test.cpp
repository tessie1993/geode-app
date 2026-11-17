// History test for the sound engine (core/sound): every analysis hop kept by its sample position.
// Built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/sound_history_test.cpp core/sound/*.cpp -o sound_history_test
//   ./sound_history_test
//
// Why: a reader that is handed a sample position (a later analysis thread, the playout clock) must get the
// same frame whoever wrote the audio and in whatever blocks. So: two engines fed one click track in different
// block sizes read identical frames at every position; a position older than the history reads the oldest hop,
// one past the newest reads the newest; between hops only the beat, bar and phrase phases move, and they move
// on exactly as step() moves them; the triggers are each hop's own, not held. At 48 kHz and at 96 kHz (halved
// before the analysis, while positions still count the input's samples).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <numbers>
#include <vector>

#include "sound/SoundEngine.hpp"

using geode::sound::SoundEngine;

namespace {

int failures = 0;
void check(bool ok, const char* what, int rate) {
    if (!ok) {
        std::printf("FAIL (%d Hz): %s\n", rate, what);
        failures++;
    }
}

constexpr double kTau = 2.0 * std::numbers::pi;
constexpr double kBpm = 174.0;
constexpr double kSeconds = 16.0;

struct Lcg {
    uint32_t state = 0x5EEDF00Du;
    int below(int n) {
        state = state * 1664525u + 1013904223u;
        return static_cast<int>((state >> 8) % static_cast<uint32_t>(n));
    }
};

// A click track with an accented first beat (a 60 Hz thump and a 3 kHz click), a function of time so it is
// the same music at every rate. Stereo, interleaved.
std::vector<float> clicks(int rate) {
    const double period = 60.0 / kBpm;
    const int64_t total = static_cast<int64_t>(kSeconds * rate);
    std::vector<float> out(static_cast<size_t>(2 * total));
    for (int64_t s = 0; s < total; s++) {
        const double t = static_cast<double>(s) / rate;
        const int k = static_cast<int>(t / period);
        const double u = t - k * period;
        const double amp = k % 4 == 0 ? 0.8 : 0.35;
        const double v = amp * std::exp(-u * 25.0) * std::sin(kTau * 60.0 * u) +
                         0.3 * std::exp(-u * 200.0) * std::sin(kTau * 3000.0 * u);
        out[static_cast<size_t>(2 * s)] = static_cast<float>(v);
        out[static_cast<size_t>(2 * s + 1)] = static_cast<float>(0.9 * v);
    }
    return out;
}

// Writes up to `end` in blocks of `block`, or of a random size when `rng` is given.
void feedTo(SoundEngine& e, const std::vector<float>& pcm, int64_t& done, int64_t end, int block, Lcg* rng) {
    while (done < end) {
        const int64_t size = std::min<int64_t>(end - done, rng != nullptr ? 1 + rng->below(3000) : block);
        e.write(pcm.data() + 2 * done, static_cast<int>(size), 2);
        done += size;
    }
}

bool same(const GeodeSoundFrame& a, const GeodeSoundFrame& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

GeodeSoundFrame withoutPhases(GeodeSoundFrame f) {
    f.beatPhase = f.barPhase = f.phrasePhase = 0.0f;
    return f;
}

// What step() makes per frame rather than per hop: the triggers it holds since the last step, and the waveform.
GeodeSoundFrame withoutStepOnly(GeodeSoundFrame f) {
    float* const triggers[] = {&f.kick,     &f.snare,    &f.hat,       &f.kick2,      &f.kick4,          &f.kick8,
                               &f.kick16,   &f.snare2,   &f.snare4,    &f.hardCut,    &f.onset,          &f.silence,
                               &f.returned, &f.surge,    &f.noteChange, &f.legato,    &f.reattack,       &f.beat,
                               &f.downbeat, &f.phrase,   &f.harmonicChange, &f.sectionChange, &f.drop,   &f.transient};
    for (float* t : triggers) *t = 0.0f;
    std::fill(std::begin(f.waveform), std::end(f.waveform), 0.0f);
    return f;
}

// Input samples per hop: the analysis hop, times the halvings in front of it.
int64_t hopInput(const SoundEngine& e) {
    return static_cast<int64_t>(e.hopSamples()) * e.sampleRateHz() / SoundEngine::analysisRateFor(e.sampleRateHz());
}

void testBlocksAndClamps(int rate) {
    const std::vector<float> pcm = clicks(rate);
    const int64_t total = static_cast<int64_t>(pcm.size() / 2);
    SoundEngine a(rate);
    SoundEngine b(rate);
    GeodeSoundFrame fa{};
    GeodeSoundFrame fb{};
    check(!a.frameAt(0, fa) && a.newestHopSample() == -1 && a.oldestHopSample() == -1, "no hop before any audio", rate);
    const int64_t hop = hopInput(a);
    Lcg rng;
    int64_t doneA = 0;
    int64_t doneB = 0;
    int compared = 0;
    int differ = 0;
    for (int64_t checkpoint = rate; checkpoint <= total; checkpoint += 2 * rate) {
        feedTo(a, pcm, doneA, checkpoint, 512, nullptr);
        feedTo(b, pcm, doneB, checkpoint, 0, &rng);
        const int64_t newest = a.newestHopSample();
        const int64_t oldest = a.oldestHopSample();
        check(newest == b.newestHopSample() && oldest == b.oldestHopSample(), "the same hops kept", rate);
        check(newest > checkpoint - hop && newest <= checkpoint, "the newest hop is the last whole one written", rate);
        for (int64_t p = oldest - 3 * hop; p <= newest + 3 * hop; p += 97) {
            check(a.frameAt(p, fa) && b.frameAt(p, fb), "a frame at every position once a hop has run", rate);
            compared++;
            if (!same(fa, fb)) differ++;
        }
    }
    check(differ == 0, "frames at the same position equal whatever the block sizes", rate);
    std::printf("  %d Hz: %d positions compared across block sizes, %d differ\n", rate, compared, differ);

    const int64_t newest = a.newestHopSample();
    const int64_t oldest = a.oldestHopSample();
    check(newest - oldest >= rate, "the history covers at least a second", rate);
    check((newest - oldest) % hop == 0, "kept hops sit a hop apart", rate);

    GeodeSoundFrame at{};
    a.frameAt(oldest, at);
    for (int64_t p : {oldest - 1, oldest - 7 * hop, int64_t{0}, std::numeric_limits<int64_t>::min()}) {
        a.frameAt(p, fa);
        check(same(fa, at), "older than the history reads the oldest hop at its own sample", rate);
    }
    a.frameAt(newest, at);
    for (int64_t p : {newest + 1, newest + hop / 2, newest + 10 * hop, std::numeric_limits<int64_t>::max()}) {
        a.frameAt(p, fa);
        check(same(withoutPhases(fa), withoutPhases(at)), "past the newest reads the newest hop", rate);
    }

    // Between hops only the phases move, forward; each hop's triggers are its own.
    int kept = 0;
    int advanced = 0;
    int beats = 0;
    for (int64_t p = oldest; p <= newest; p += hop) {
        kept++;
        a.frameAt(p, at);
        a.frameAt(p + hop / 2, fa);
        a.frameAt(p + hop - 1, fb);
        check(same(withoutPhases(fa), withoutPhases(at)) && same(withoutPhases(fb), withoutPhases(at)),
              "within a hop only the phases move", rate);
        if (fa.beatPhase > at.beatPhase && fb.beatPhase > fa.beatPhase) advanced++;
        if (at.beat > 0.0f) {
            beats++;
            GeodeSoundFrame before{};
            GeodeSoundFrame after{};
            a.frameAt(p - hop, before);
            a.frameAt(p + hop, after);
            check(p == oldest || before.beat == 0.0f, "a beat is its own hop's, not held from before", rate);
            check(p == newest || after.beat == 0.0f, "a beat is its own hop's, not held after", rate);
        }
    }
    std::printf("  %d Hz: %d hops kept, the beat phase moved on within %d, %d beat hops\n", rate, kept, advanced, beats);
    check(advanced * 2 > kept, "the beat phase moves on between hops once locked", rate);
    check(beats > 0, "a beat among the kept hops", rate);

    a.reset();
    check(!a.frameAt(newest, fa) && a.newestHopSample() == -1, "reset() forgets every hop", rate);
}

// At the position step() has been written to, frameAt() reads what step() does, the phases included, bit for bit.
void testMatchesStep(int rate) {
    const std::vector<float> pcm = clicks(rate);
    const int64_t total = static_cast<int64_t>(pcm.size() / 2);
    SoundEngine e(rate);
    GeodeSoundFrame stepped{};
    GeodeSoundFrame read{};
    int64_t done = 0;
    int compared = 0;
    int differ = 0;
    while (done + 1237 <= total) {
        e.write(pcm.data() + 2 * done, 1237, 2);
        done += 1237;
        e.step(1.0f / 60.0f, stepped);
        check(e.frameAt(done, read), "a frame where step() reads one", rate);
        compared++;
        if (!same(withoutStepOnly(stepped), withoutStepOnly(read))) differ++;
    }
    check(differ == 0, "frameAt(written) equals step() but for its held triggers and waveform", rate);
    std::printf("  %d Hz: %d steps compared with frameAt, %d differ\n", rate, compared, differ);
}

}  // namespace

int main() {
    std::printf("sound history: hops by sample position\n");
    for (int rate : {48000, 96000}) {
        testBlocksAndClamps(rate);
        testMatchesStep(rate);
    }
    if (failures == 0) std::printf("sound_history_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
