// Host test for the renderer's read of a published analysis (core/sound/interp/FrameBlend, #1215): a surface asks
// for the frame at the sample it draws and gets each field by its rule between the two hops around it.
// Built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore core/tests/frame_blend_test.cpp
//     core/sound/interp/FrameBlend.cpp core/sound/hop/AnalysisThread.cpp core/sound/*.cpp
//     core/audio/ingest/PcmRing.cpp -o frame_blend_test
//   ./frame_blend_test
//
// Why: the renderer no longer analyses. Its frame must equal the engine's at every hop, move monotonically between
// two hops, fire every hit exactly once at whatever rate it draws (with the sample it fired at), read the same for two
// surfaces on one ring, find both hops around a speaker that trails the writer, and an export's frames must agree at
// 30, 60 and 120 fps. No hop length is assumed: positions come from the published hops.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

#include "audio/ingest/PcmRing.hpp"
#include "sound/hop/AnalysisThread.hpp"
#include "sound/hop/SharedAnalysis.hpp"
#include "sound/interp/FrameBlend.hpp"

using geode::audio::ingest::PcmRing;
using geode::sound::AnalysisThread;
using geode::sound::Emotion;
using geode::sound::SharedAnalysis;
using geode::sound::interp::blendSteps;
using geode::sound::interp::FieldKind;
using geode::sound::interp::FrameReader;
using geode::sound::interp::Hits;
using geode::sound::interp::kindOf;
using Heard = AnalysisThread::Heard;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

constexpr int kRate = 48000;
constexpr double kTau = 2.0 * std::numbers::pi;
constexpr int kRingFrames = 1 << 16;
constexpr int kRunway = 1 << 12;
constexpr int64_t kAll = std::numeric_limits<int64_t>::max();
constexpr int kFloats = GEODE_SOUND_FRAME_FLOATS;

struct Lcg {
    uint32_t state;
    int below(int n) {
        state = state * 1664525u + 1013904223u;
        return static_cast<int>((state >> 8) % static_cast<uint32_t>(n));
    }
};

// A click track with an accented first beat and a held tone under it, so levels, pitch and hits all move. Stereo.
std::vector<float> music(double bpm, double seconds) {
    const double period = 60.0 / bpm;
    const int64_t total = static_cast<int64_t>(seconds * kRate);
    std::vector<float> out(static_cast<size_t>(2 * total));
    for (int64_t s = 0; s < total; s++) {
        const double t = static_cast<double>(s) / kRate;
        const int k = static_cast<int>(t / period);
        const double u = t - k * period;
        const double amp = k % 4 == 0 ? 0.8 : 0.35;
        const double tone = 0.1 * std::sin(kTau * (k % 8 < 4 ? 220.0 : 330.0) * t);
        const double v = amp * std::exp(-u * 25.0) * std::sin(kTau * 60.0 * u) +
                         0.3 * std::exp(-u * 200.0) * std::sin(kTau * 3000.0 * u) + tone;
        out[static_cast<size_t>(2 * s)] = static_cast<float>(v);
        out[static_cast<size_t>(2 * s + 1)] = static_cast<float>(0.9 * v);
    }
    return out;
}

const float* floats(const GeodeSoundFrame& f) { return reinterpret_cast<const float*>(&f); }

// Every field the rule table carries from a hop (not the triggers, the player's fade or the drawn waveform).
bool sameCarried(const GeodeSoundFrame& a, const GeodeSoundFrame& b) {
    const float* x = floats(a);
    const float* y = floats(b);
    for (int i = 0; i < kFloats; i++) {
        const FieldKind k = kindOf(i);
        if (k == FieldKind::Trigger || k == FieldKind::Told || k == FieldKind::Drawn) continue;
        if (std::memcmp(&x[i], &y[i], sizeof(float)) != 0) return false;
    }
    return true;
}

bool sameEmotion(Emotion::State a, Emotion::State b) {
    a.degreeAccent = 0.0f;
    b.degreeAccent = 0.0f;
    return std::memcmp(&a, &b, sizeof a) == 0;
}

std::vector<int> triggerIndices() {
    std::vector<int> out;
    for (int i = 0; i < kFloats; i++) {
        if (kindOf(i) == FieldKind::Trigger) out.push_back(i);
    }
    return out;
}

// Writes [from, to) frames of `pcm` in blocks, pumping after each.
void writeRange(PcmRing& ring, AnalysisThread& at, const std::vector<float>& pcm, int64_t from, int64_t to, Lcg* jitter) {
    for (int64_t s = from; s < to;) {
        const int n = static_cast<int>(std::min<int64_t>(to - s, jitter ? 64 + jitter->below(2000) : 1024));
        ring.writeFloat(pcm.data() + static_cast<size_t>(2 * s), n, 2, kRate);
        at.pump(0.0f);
        s += n;
    }
}

// 1. The rule table: at a hop the read is the engine's frame; between two hops every level lies between theirs and
// moves one way, a clock that ran forward does too, and the phases are the engine's at that sample.
void testRules() {
    const std::vector<float> pcm = music(97.0, 3.0);
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, false);
    Lcg jitter{7};
    writeRange(ring, at, pcm, 0, static_cast<int64_t>(pcm.size() / 2), &jitter);
    std::vector<Heard> hops;
    at.hopsBetween(-1, kAll, [&](const Heard& h) { hops.push_back(h); });
    check(hops.size() > 100, "rules: hops published");
    int atHop = 0;
    int between = 0;
    int unordered = 0;
    int outside = 0;
    int phaseOff = 0;
    for (size_t k = 0; k + 1 < hops.size(); k++) {
        const Heard& a = hops[k];
        const Heard& b = hops[k + 1];
        GeodeSoundFrame engine{};
        GeodeSoundFrame read{};
        at.frameAt(a.position, engine);
        blendSteps(a, &b, a.position, read);
        if (sameCarried(engine, read)) atHop++;
        FrameReader reader;
        Emotion::State feel{};
        Hits hits;
        GeodeSoundFrame first{};
        check(reader.read(at, a.position, first, feel, hits) && sameCarried(engine, first), "rules: a read at a hop");
        const float* fa = floats(a.frame);
        const float* fb = floats(b.frame);
        GeodeSoundFrame previous = read;
        for (int j = 1; j < 8; j++) {
            const int64_t p = a.position + (b.position - a.position) * j / 8;
            if (p <= a.position || p >= b.position) continue;
            Heard lo;
            Heard hi;
            check(at.hopAt(p, lo) && lo.index == a.index && at.hopAfter(p, hi) && hi.index == b.index,
                  "rules: the two hops around a sample");
            GeodeSoundFrame mid{};
            blendSteps(a, &b, p, mid);
            between++;
            const float* m = floats(mid);
            const float* q = floats(previous);
            for (int i = 0; i < kFloats; i++) {
                const FieldKind kind = kindOf(i);
                const bool line = kind == FieldKind::Linear || (kind == FieldKind::Clock && fb[i] >= fa[i]);
                if (!line) continue;
                const float lo2 = std::min(fa[i], fb[i]);
                const float hi2 = std::max(fa[i], fb[i]);
                if (m[i] < lo2 || m[i] > hi2) outside++;
                if ((m[i] - q[i]) * (fb[i] - fa[i]) < 0.0f) unordered++;
            }
            if (a.clock.locked && a.clock.period > 0.0f) {
                GeodeSoundFrame there{};
                at.frameAt(p, there);
                if (mid.beatPhase != there.beatPhase || mid.barPhase != there.barPhase || mid.phrasePhase != there.phrasePhase) {
                    phaseOff++;
                }
            }
            for (int i = 0; i < kFloats; i++) {
                if (kindOf(i) == FieldKind::Trigger && m[i] != 0.0f) outside++;
            }
            previous = mid;
        }
    }
    check(atHop == static_cast<int>(hops.size()) - 1, "rules: at every hop the read is the engine's frame");
    check(between > 0 && outside == 0, "rules: between two hops a level lies between theirs, a trigger is 0");
    check(unordered == 0, "rules: between two hops a level moves one way");
    check(phaseOff == 0, "rules: the phases are the engine's at the sample");
}

// The hops published since the last call, by index: what a surface may be handed.
struct Truth {
    struct Hop {
        uint64_t index;
        int64_t position;
        std::vector<float> triggers;
    };
    std::vector<Hop> hops;
    int64_t seen = -1;
    int64_t last = -1;
    void collect(const AnalysisThread& at, const std::vector<int>& triggers) {
        at.hopsBetween(last - 1, kAll, [&](const Heard& h) {
            if (static_cast<int64_t>(h.index) <= seen) return;
            seen = static_cast<int64_t>(h.index);
            last = h.position;
            Hop hop{h.index, h.position, {}};
            for (const int i : triggers) hop.triggers.push_back(floats(h.frame)[i]);
            hops.push_back(std::move(hop));
        });
    }
};

// 2. Hits: whatever the frame boundaries, each frame fires exactly the hops published since the last that lie at or
// before its sample, each at its strongest, with the sample it fired at; none twice, none lost, none early. The
// reader trails the writer like a speaker, and a run where the analysis lags the reader delivers late hops once.
int runHits(double fps, Lcg* jitter, bool analysisLags, const std::vector<float>& pcm, int* firedFrames) {
    const std::vector<int> triggers = triggerIndices();
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, false);
    FrameReader reader;
    Truth truth;
    const int64_t total = static_cast<int64_t>(pcm.size() / 2);
    const int64_t lag = analysisLags ? 0 : 12000;  // the speaker trails the writer by the sink's buffer
    int64_t written = 0;
    int64_t speaker = 0;
    size_t pending = 0;  // truth.hops before this were delivered (or heard before the first read)
    bool primed = false;
    int bad = 0;
    int fired = 0;
    for (int frame = 1; written < total; frame++) {
        const int64_t end = jitter ? std::min(total, written + jitter->below(3000)) :
                                     std::min(total, static_cast<int64_t>(std::llround(frame * kRate / fps)));
        for (int64_t s = written; s < end;) {
            const int n = static_cast<int>(std::min<int64_t>(end - s, kRunway));
            ring.writeFloat(pcm.data() + static_cast<size_t>(2 * s), n, 2, kRate);
            s += n;
        }
        written = end;
        speaker = std::max<int64_t>(speaker, written - lag);
        // Lagging: the reader asks before the analysis ran, then again at the same sample once it has.
        for (int pass = analysisLags ? 0 : 1; pass < 2; pass++) {
            if (pass == 1) at.pump(0.0f);
            truth.collect(at, triggers);
            GeodeSoundFrame out{};
            Emotion::State feel{};
            Hits hits;
            if (!reader.read(at, speaker, out, feel, hits)) continue;
            if (!primed) {
                // The first read hears from here on.
                while (pending < truth.hops.size() && truth.hops[pending].position <= speaker) pending++;
                primed = true;
                continue;
            }
            std::vector<float> expect(triggers.size(), 0.0f);
            std::vector<int64_t> where(triggers.size(), -1);
            for (; pending < truth.hops.size() && truth.hops[pending].position <= speaker; pending++) {
                const Truth::Hop& h = truth.hops[pending];
                for (size_t t = 0; t < triggers.size(); t++) {
                    if (h.triggers[t] > 0.0f && h.triggers[t] >= expect[t]) {
                        expect[t] = h.triggers[t];
                        where[t] = h.position;
                    }
                }
            }
            bool any = false;
            for (size_t t = 0; t < triggers.size(); t++) {
                const float got = floats(out)[triggers[t]];
                const int64_t firedAt = hits.at[static_cast<size_t>(triggers[t])];
                if (got != expect[t] || firedAt != where[t]) {
                    if (bad == 0) {
                        std::printf("  fps %.0f: first miss at sample %lld, trigger %d: fired %g at %lld, expected %g at %lld\n",
                                    fps, static_cast<long long>(speaker), triggers[t], static_cast<double>(got),
                                    static_cast<long long>(firedAt), static_cast<double>(expect[t]), static_cast<long long>(where[t]));
                    }
                    bad++;
                }
                any = any || got > 0.0f;
            }
            fired += any ? 1 : 0;
        }
    }
    if (bad > 0) std::printf("  fps %.0f: %d trigger reads differ, %d frames fired\n", fps, bad, fired);
    if (firedFrames) *firedFrames = fired;
    return bad;
}

void testHits() {
    const std::vector<float> pcm = music(131.0, 6.0);
    for (const double fps : {30.0, 60.0, 120.0, 47.0}) {
        int fired = 0;
        check(runHits(fps, nullptr, false, pcm, &fired) == 0, "hits: every frame fires exactly its hops, at any rate");
        check(fired > 10, "hits: frames fired");
    }
    Lcg jitter{99};
    check(runHits(0.0, &jitter, false, pcm, nullptr) == 0, "hits: irregular frames, some at the same sample");
    Lcg late{5};
    check(runHits(0.0, &late, true, pcm, nullptr) == 0, "hits: a hop published after its sample was read fires once");
}

// 3. Two surfaces on one ring's shared analysis (its own thread, woken by the ring's writes) read the same frame at
// the same sample, equal to an inline analysis of the same audio; and a reader at a speaker that trails the writer
// finds a hop on each side of its sample.
bool caughtUp(const AnalysisThread& shared, const AnalysisThread& inline_) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (shared.newestPosition() < inline_.newestPosition()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

void testShared() {
    const std::vector<float> pcm = music(89.0, 4.0);
    const int64_t total = static_cast<int64_t>(pcm.size() / 2);
    auto ring = std::make_shared<PcmRing>(kRingFrames, 2, kRunway);
    auto shared = std::make_shared<SharedAnalysis>(ring, false);
    PcmRing refRing(kRingFrames, 2, kRunway);
    AnalysisThread ref(refRing, false);
    SharedAnalysis::Listening first(shared);
    SharedAnalysis::Listening second(shared);
    check(shared->thread().running(), "shared: the first listener starts the thread");
    FrameReader one;
    FrameReader two;
    FrameReader alone;
    const int64_t lead = 12000;
    int differ = 0;
    int bracketed = 0;
    int reads = 0;
    bool kept = true;
    Lcg blocks{3};
    for (int64_t s = 0; s < total && kept;) {
        const int n = static_cast<int>(std::min<int64_t>(total - s, 200 + blocks.below(1800)));
        ring->writeFloat(pcm.data() + static_cast<size_t>(2 * s), n, 2, kRate);
        refRing.writeFloat(pcm.data() + static_cast<size_t>(2 * s), n, 2, kRate);
        ref.pump(0.0f);
        s += n;
        kept = caughtUp(shared->thread(), ref);
        const int64_t speaker = s - lead;
        if (speaker <= 0) continue;
        GeodeSoundFrame a{};
        GeodeSoundFrame b{};
        GeodeSoundFrame c{};
        Emotion::State ea{};
        Emotion::State eb{};
        Emotion::State ec{};
        Hits ha;
        Hits hb;
        Hits hc;
        const bool ok = one.read(shared->thread(), speaker, a, ea, ha) && two.read(shared->thread(), speaker, b, eb, hb) &&
                        alone.read(ref, speaker, c, ec, hc);
        if (!ok) continue;
        reads++;
        if (std::memcmp(&a, &b, sizeof a) != 0 || std::memcmp(&a, &c, sizeof a) != 0 || ha.at != hb.at || ha.at != hc.at ||
            !sameEmotion(ea, eb) || !sameEmotion(ea, ec)) {
            differ++;
        }
        Heard lo;
        Heard hi;
        if (shared->thread().hopAt(speaker, lo) && shared->thread().hopAfter(speaker, hi) && lo.position <= speaker &&
            speaker < hi.position) {
            bracketed++;
        }
    }
    check(kept, "shared: the thread keeps up with the writes that wake it");
    check(reads > 50 && differ == 0, "shared: two surfaces and an inline analysis read the same frame at one sample");
    check(bracketed == reads, "look-ahead: a speaker behind the writer has a hop on each side");
    first = {};
    check(shared->thread().running(), "shared: one listener left keeps it running");
    second = {};
    check(!shared->thread().running(), "shared: the last listener gone stops it");
}

// 4. An export at 30, 60 and 120 fps (inline, written up to each frame's end, read at that sample): every carried field
// equal at a shared sample, and each 30 fps frame's triggers the strongest of the faster frames it spans.
struct Run {
    std::vector<GeodeSoundFrame> frames;
    std::vector<Emotion::State> feel;
};
Run exportRun(int fps, const std::vector<float>& pcm, int seconds) {
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, true);
    at.setPlayerSource(true);
    FrameReader reader;
    Run run;
    int64_t written = 0;
    for (int frame = 1; frame <= fps * seconds; frame++) {
        const int64_t end = static_cast<int64_t>(frame) * kRate / fps;
        for (int64_t s = written; s < end;) {
            const int n = static_cast<int>(std::min<int64_t>(end - s, kRunway));
            ring.writeFloat(pcm.data() + static_cast<size_t>(2 * s), n, 2, kRate);
            s += n;
        }
        written = end;
        at.pump(1.0f / static_cast<float>(fps));
        GeodeSoundFrame out{};
        Emotion::State feel{};
        Hits hits;
        reader.read(at, ring.headFrames(), out, feel, hits);
        run.frames.push_back(out);
        run.feel.push_back(feel);
    }
    return run;
}

void testExportRates() {
    constexpr int kSeconds = 6;
    const std::vector<float> pcm = music(113.0, kSeconds);
    const Run r30 = exportRun(30, pcm, kSeconds);
    const Run r60 = exportRun(60, pcm, kSeconds);
    const Run r120 = exportRun(120, pcm, kSeconds);
    const std::vector<int> triggers = triggerIndices();
    int carried = 0;
    int felt = 0;
    int held = 0;
    int compared = 0;
    // From the fourth 30 fps frame: every rate has read once before the stretch compared.
    for (size_t k = 4; k <= r30.frames.size(); k++) {
        const GeodeSoundFrame& f30 = r30.frames[k - 1];
        const GeodeSoundFrame& f60 = r60.frames[2 * k - 1];
        const GeodeSoundFrame& f120 = r120.frames[4 * k - 1];
        compared++;
        if (!sameCarried(f30, f60) || !sameCarried(f30, f120)) carried++;
        if (!sameEmotion(r30.feel[k - 1], r60.feel[2 * k - 1]) || !sameEmotion(r30.feel[k - 1], r120.feel[4 * k - 1])) felt++;
        for (const int i : triggers) {
            const float s60 = std::max(floats(r60.frames[2 * k - 2])[i], floats(f60)[i]);
            float s120 = 0.0f;
            for (size_t j = 4 * k - 4; j < 4 * k; j++) s120 = std::max(s120, floats(r120.frames[j])[i]);
            if (floats(f30)[i] != s60 || floats(f30)[i] != s120) held++;
        }
        const float a60 = std::max(r60.feel[2 * k - 2].degreeAccent, r60.feel[2 * k - 1].degreeAccent);
        if (r30.feel[k - 1].degreeAccent != a60) held++;
    }
    check(compared > 100, "export: frames compared");
    check(carried == 0, "export: every carried field equal at 30, 60 and 120 fps at a shared sample");
    check(felt == 0, "export: the feeling equal at a shared sample");
    check(held == 0, "export: a 30 fps frame's triggers are the strongest of the faster frames it spans");
}

// 5. A paused speaker. Its frames show where the analysis stands and fire only the hops the starved engine ran on
// silence; the hits the sink still held fire when the speaker plays them, each once, and the silence does not fire again.
void testPause() {
    const std::vector<int> triggers = triggerIndices();
    const std::vector<float> pcm = music(113.0, 4.0);
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, true);
    FrameReader reader;
    Truth truth;
    const int64_t total = static_cast<int64_t>(pcm.size() / 2) - 123;  // not on a hop: the silence stands alone at the head
    const int64_t lag = 30000;  // the sink's buffer, over a beat long
    const int64_t step = kRate / 60;
    GeodeSoundFrame out{};
    Emotion::State feel{};
    Hits hits;
    int64_t written = 0;
    int64_t speaker = 0;
    // Playing: the writer a sink's buffer ahead of the speaker.
    while (written < total) {
        const int n = static_cast<int>(std::min<int64_t>(step, total - written));
        ring.writeFloat(pcm.data() + static_cast<size_t>(2 * written), n, 2, kRate);
        written += n;
        at.pump(0.0f);
        truth.collect(at, triggers);
        speaker = std::max<int64_t>(0, written - lag);
        reader.read(at, speaker, out, feel, hits);
    }
    size_t pending = 0;
    while (pending < truth.hops.size() && truth.hops[pending].position <= speaker) pending++;
    int owed = 0;
    for (size_t h = pending; h < truth.hops.size(); h++) {
        for (const float v : truth.hops[h].triggers) owed += v > 0.0f ? 1 : 0;
    }
    check(owed > 0, "pause: the sink holds hits the speaker has not played");
    const size_t played = truth.hops.size();

    // Paused: the writer stops, the speaker stands, the analysis runs on silence.
    const int silenceIndex = static_cast<int>(offsetof(GeodeSoundFrame, silence) / sizeof(float));
    int early = 0;
    int silences = 0;
    bool shown = true;
    for (int frame = 0; frame < 120; frame++) {
        at.pump(1.0f / 60.0f);
        truth.collect(at, triggers);
        shown = reader.readStill(at, out, feel, hits) && shown;
        for (const int t : triggers) {
            if (floats(out)[t] <= 0.0f) continue;
            if (hits.at[static_cast<size_t>(t)] != written) early++;
            if (t == silenceIndex) silences++;
        }
    }
    check(truth.hops.size() > played, "pause: the starved engine ran hops on silence");
    check(shown && early == 0, "pause: a paused frame fires no hit the speaker has not played");
    check(silences == 1, "pause: the silence fires once, while paused");

    // Resumed: the speaker plays what the sink held.
    int bad = 0;
    int again = 0;
    while (speaker < written) {
        speaker = std::min(written, speaker + step);
        reader.read(at, speaker, out, feel, hits);
        std::vector<float> expect(triggers.size(), 0.0f);
        for (; pending < played && truth.hops[pending].position <= speaker; pending++) {
            for (size_t t = 0; t < triggers.size(); t++) expect[t] = std::max(expect[t], truth.hops[pending].triggers[t]);
        }
        for (size_t t = 0; t < triggers.size(); t++) {
            if (floats(out)[triggers[t]] != expect[t]) bad++;
        }
        again += out.silence > 0.0f ? 1 : 0;
    }
    check(bad == 0 && pending == played, "pause: the hits the sink held fire as the speaker plays them, each once");
    check(again == 0, "pause: the silence heard while paused does not fire again");
}

}  // namespace

int main() {
    testRules();
    testHits();
    testShared();
    testExportRates();
    testPause();
    if (failures == 0) std::printf("frame_blend_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
