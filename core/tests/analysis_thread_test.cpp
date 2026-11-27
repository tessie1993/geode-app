// Host test for the analysis thread (core/sound/hop/AnalysisThread, #1215): one sound engine fed from a PCM ring
// on its own thread, every hop published by ring position for readers on other threads.
// Built with the runner's own compiler in CI, and again under ThreadSanitizer:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore core/tests/analysis_thread_test.cpp
//     core/sound/hop/AnalysisThread.cpp core/sound/*.cpp core/audio/ingest/PcmRing.cpp -o analysis_thread_test
//   ./analysis_thread_test
//
// Why: a renderer, an export and an event reader will ask "what was heard at ring position P" of one engine that
// runs elsewhere, so the answer must be the bare engine's, bit for bit, whoever wrote the ring in whatever blocks,
// across a new epoch and a control sent mid-stream; a reader racing the writer must never see a torn hop; the
// thread must sleep when there is nothing to do, and stop cleanly.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <numbers>
#include <thread>
#include <vector>

#include "audio/ingest/PcmRing.hpp"
#include "sound/SoundEngine.hpp"
#include "sound/hop/AnalysisThread.hpp"

using geode::audio::ingest::PcmRing;
using geode::sound::AnalysisThread;
using geode::sound::Emotion;
using geode::sound::Rhythm;
using geode::sound::SoundEngine;
using Heard = AnalysisThread::Heard;

namespace geode::sound {
// The test's way to the control queue: hold its lock from another thread, count what waits.
struct AnalysisThreadTestAccess {
    static std::mutex& lock(AnalysisThread& at) { return at.controlLock_; }
    static int waiting(AnalysisThread& at) {
        std::lock_guard<std::mutex> l(at.controlLock_);
        return at.controlCount_;
    }
    // The applied tuning: read only with the thread stopped or never started.
    static GeodeSoundTuning tuning(const AnalysisThread& at) { return at.tuning_; }
};
}  // namespace geode::sound
using geode::sound::AnalysisThreadTestAccess;
using Clock = std::chrono::steady_clock;

namespace {

std::atomic<int> failures{0};
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

constexpr double kTau = 2.0 * std::numbers::pi;
constexpr int kRingFrames = 1 << 16;
constexpr int kRunway = 1 << 12;
constexpr int64_t kAll = std::numeric_limits<int64_t>::max();

struct Lcg {
    uint32_t state;
    int below(int n) {
        state = state * 1664525u + 1013904223u;
        return static_cast<int>((state >> 8) % static_cast<uint32_t>(n));
    }
};

// A click track with an accented first beat (a 60 Hz thump and a 3 kHz click). Stereo, interleaved.
std::vector<float> clicks(int rate, double bpm, double seconds) {
    const double period = 60.0 / bpm;
    const int64_t total = static_cast<int64_t>(seconds * rate);
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

bool same(const GeodeSoundFrame& a, const GeodeSoundFrame& b) { return std::memcmp(&a, &b, sizeof a) == 0; }
bool same(const Emotion::State& a, const Emotion::State& b) { return std::memcmp(&a, &b, sizeof a) == 0; }
bool same(const Rhythm::Clock& a, const Rhythm::Clock& b) {
    return a.theta == b.theta && a.period == b.period && a.lastBeat == b.lastBeat && a.hop == b.hop &&
           a.meter == b.meter && a.downSlot == b.downSlot && a.barInPhrase == b.barInPhrase && a.locked == b.locked;
}
// Field by field: the padding of a published copy is not part of it.
bool same(const Heard& a, const Heard& b) {
    return a.index == b.index && a.position == b.position && same(a.clock, b.clock) && a.halvings == b.halvings &&
           same(a.frame, b.frame) && same(a.emotion, b.emotion);
}

// The bare engine's kept hop at engine position `sample`, newest first as frameAt picks.
const SoundEngine::Hop* keptAt(const SoundEngine& e, int64_t sample) {
    for (int age = 0; const SoundEngine::Hop* h = e.keptHop(age); age++) {
        if (h->sample == sample) return h;
    }
    return nullptr;
}

int threadCount() {
    std::error_code error;
    int n = 0;
    for (std::filesystem::directory_iterator it("/proc/self/task", error), end; !error && it != end; it.increment(error)) n++;
    return error ? -1 : n;
}

// Back to `before` threads: a joined thread can still be listed for a moment after join() returns.
bool threadsBackTo(int before) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(100);
    while (threadCount() != before && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return threadCount() == before;
}

// Every hop published since the last call against a bare engine whose position 0 is ring position `base`: the frame
// and feeling at the hop, its clock, and a position between hops, where only the phases move.
struct Compared {
    int64_t checked = -1;
    int hops = 0;
    int differ = 0;
};
void compareNew(const AnalysisThread& at, const SoundEngine& bare, int64_t base, Compared& c) {
    GeodeSoundFrame mine{};
    GeodeSoundFrame theirs{};
    at.hopsBetween(c.checked, kAll, [&](const Heard& h) {
        Emotion::State feeling{};
        const bool read = at.frameAt(h.position, mine, &feeling) && bare.frameAt(h.position - base, theirs);
        const SoundEngine::Hop* kept = keptAt(bare, h.position - base);
        c.hops++;
        if (!read || kept == nullptr || !same(mine, theirs) || !same(feeling, kept->emotion) || !same(h.clock, kept->clock)) {
            c.differ++;
        }
        const int64_t between = h.position + 211;
        at.frameAt(between, mine);
        bare.frameAt(between - base, theirs);
        if (!same(mine, theirs)) c.differ++;
        c.checked = h.position;
    });
}

// Inline (pump()) against a bare SoundEngine fed the same frames: every published hop, and positions between hops,
// read the same frame and feeling, across irregular blocks, a new epoch mid-stream and, with `controls`, a tuning
// change, a track boundary, a reset, an emotion reset and the player's fade sent mid-stream.
void inlineMatchesBare(int rate, bool controls) {
    const std::vector<float> pcm = clicks(rate, controls ? 93.0 : 141.0, 8.0);
    const int64_t total = static_cast<int64_t>(pcm.size() / 2);
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, false);
    SoundEngine bare(rate);
    bare.setAutoGain(true);  // the thread's source is not the player until it is told otherwise
    GeodeSoundFrame none{};
    check(!at.frameAt(0, none) && at.newestPosition() == -1, "no hop before any audio");
    Lcg rng{0x5EEDF00Du + static_cast<uint32_t>(rate)};
    int64_t base = 0;  // the ring position of the bare engine's position 0
    int64_t done = 0;
    Compared c;
    int block = 0;
    bool epoch = false;
    while (done < total) {
        if (!epoch && done >= total / 2) {
            ring.beginEpoch();
            bare.reset();
            base = ring.headFrames();
            epoch = true;
        }
        if (controls) {
            if (block == 40) {
                GeodeSoundTuning t = SoundEngine::defaultTuning();
                t.gain[0] = 2.0f;
                t.drum[0] = 0.4f;
                check(at.setTuning(t), "a tuning is queued");
                bare.setTuning(t);
            } else if (block == 70) {
                check(at.trackBoundary(), "a boundary is queued");
                bare.trackBoundary();
            } else if (block == 90) {
                at.setPlayerSource(true);
                at.setFadeGain(0.4f);
                bare.setPlayerSource(true);
                bare.setAutoGain(false);
                bare.setFadeGain(0.4f);
            } else if (block == 110) {
                check(at.resetEmotion(), "an emotion reset is queued");
                bare.resetEmotion();
            } else if (block == 200) {
                check(at.reset(), "a reset is queued");
                bare.reset();
                base = ring.headFrames();
            }
        }
        const int size = static_cast<int>(std::min<int64_t>(total - done, 1 + rng.below(3000)));
        ring.writeFloat(pcm.data() + 2 * done, size, 2, rate);
        bare.write(pcm.data() + 2 * done, size, 2);
        done += size;
        block++;
        at.pump(0.01f);
        compareNew(at, bare, base, c);
    }
    std::printf("  %d Hz%s: %d hops compared with the bare engine, %d differ\n", rate, controls ? " with controls" : "",
                c.hops, c.differ);
    // The hop is a sample count: the audio holds total / (hop × 2^halvings) of them, and nearly all are published.
    const int64_t hopInput = static_cast<int64_t>(bare.hopSamples()) << bare.halvings();
    check(c.hops > total / hopInput * 9 / 10, "a hop published for nearly every hop of audio written");
    check(c.differ == 0, "inline frames equal the bare engine's at every position");

    // Older than every kept hop reads the oldest, past the newest the newest; hopAt picks as frameAt does.
    Heard oldest;
    Heard h;
    check(at.hopAt(std::numeric_limits<int64_t>::min(), oldest), "a hop older than the kept ones");
    check(at.hopAt(oldest.position - 1, h) && h.index == oldest.index, "older than the kept hops reads the oldest");
    check(at.hopAt(kAll, h) && h.position == at.newestPosition(), "past the newest reads the newest");
    int span = 0;
    at.hopsBetween(oldest.position - 1, kAll, [&](const Heard&) { span++; });
    check(span == at.published() - 1, "every readable hop is visited once");
}

// A stall longer than one take (the cursor keeps the newest runway and re-bases) and a rate change mid-stream (a
// fresh engine at the new rate, positions re-based on the ring) still read exactly what a bare engine reads.
void inlineSkipAndRate() {
    const std::vector<float> first = clicks(48000, 77.0, 4.0);
    const std::vector<float> second = clicks(44100, 77.0, 3.0);
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, false);
    SoundEngine bare48(48000);
    bare48.setAutoGain(true);  // the thread's source is not the player
    Compared c;
    Lcg rng{174u};
    int64_t base = 0;
    bool skipped = false;
    const int64_t total48 = static_cast<int64_t>(first.size() / 2);
    for (int64_t done = 0; done < total48;) {
        int size = static_cast<int>(std::min<int64_t>(total48 - done, 1 + rng.below(3000)));
        if (!skipped && done >= total48 / 2 && total48 - done > 3 * kRunway) {
            // Three runways between two pumps: the cursor hands out the newest runway and reports a skip.
            size = 3 * kRunway;
            bare48.write(first.data() + 2 * (done + size - kRunway), kRunway, 2);
            base += size - kRunway;
            skipped = true;
        } else {
            bare48.write(first.data() + 2 * done, size, 2);
        }
        ring.writeFloat(first.data() + 2 * done, size, 2, 48000);
        done += size;
        at.pump(0.01f);
        compareNew(at, bare48, base, c);
    }
    const int before = c.hops;
    SoundEngine bare44(44100);
    bare44.setAutoGain(true);
    base = ring.headFrames();
    const int64_t total44 = static_cast<int64_t>(second.size() / 2);
    for (int64_t done = 0; done < total44;) {
        const int size = static_cast<int>(std::min<int64_t>(total44 - done, 1 + rng.below(3000)));
        ring.writeFloat(second.data() + 2 * done, size, 2, 44100);
        bare44.write(second.data() + 2 * done, size, 2);
        done += size;
        at.pump(0.01f);
        compareNew(at, bare44, base, c);
    }
    std::printf("  skip and 48000 -> 44100: %d hops before the switch, %d after, %d differ\n", before, c.hops - before,
                c.differ);
    // Four fifths of the hops each side's audio holds (the skip drops two runways of it).
    const int64_t heard48 = (total48 - 2 * kRunway) / bare48.hopSamples();
    const int64_t heard44 = total44 / bare44.hopSamples();
    check(skipped && before > heard48 * 4 / 5 && c.hops - before > heard44 * 4 / 5, "hops on both sides of the skip and the switch");
    check(c.differ == 0, "after a skip and a rate change the frames equal a bare engine's");
}

// The writer stays within one cursor take of the thread (a stall longer than that keeps the newest audio, Cursor's
// rule), so the threaded run hears every frame, as the inline one does.
bool paced(const PcmRing& ring, const AnalysisThread& at, Clock::time_point deadline) {
    while (ring.headFrames() - std::max<int64_t>(0, at.newestPosition()) > kRunway / 2) {
        if (Clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

// The thread against the inline run: the same hops, readers on two other threads never see a torn one, and the
// thread wakes about once per write, not on a clock.
void threadedMatchesInline() {
    const int rate = 48000;
    const std::vector<float> pcm = clicks(rate, 174.0, 8.0);
    const int64_t total = static_cast<int64_t>(pcm.size() / 2);

    std::vector<Heard> expected;
    {
        PcmRing ring(kRingFrames, 2, kRunway);
        AnalysisThread at(ring, false);
        Lcg rng{77u};
        int64_t last = -1;
        for (int64_t done = 0; done < total;) {
            const int size = static_cast<int>(std::min<int64_t>(total - done, 1 + rng.below(1000)));
            ring.writeFloat(pcm.data() + 2 * done, size, 2, rate);
            done += size;
            at.pump(0.01f);
            at.hopsBetween(last, kAll, [&](const Heard& h) {
                check(h.index == expected.size(), "hops are published in order");
                expected.push_back(h);
                last = h.position;
            });
        }
    }

    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, false);
    check(at.start(), "the thread starts");
    std::atomic<bool> writing{true};
    std::atomic<int> reads{0};
    std::atomic<int> torn{0};
    std::atomic<int> wrong{0};
    std::atomic<int> edgeReads{0};
    // Near the newest: the hop at or before p, and frameAt there.
    const auto reader = [&](uint32_t seed) {
        Lcg rng{seed};
        Heard h;
        GeodeSoundFrame f{};
        while (writing.load(std::memory_order_acquire)) {
            const int64_t newest = at.newestPosition();
            if (newest < 0) continue;
            const int64_t p = newest - rng.below(10000);
            if (at.hopAt(p, h)) {
                reads.fetch_add(1, std::memory_order_relaxed);
                if (h.index >= expected.size() || !same(h, expected[h.index])) {
                    torn.fetch_add(1);
                } else if (h.position <= p && h.index + 1 < expected.size() && expected[h.index + 1].position <= p) {
                    // A newer hop at or before p was already published (p is at or below the newest): too old a pick.
                    // A hop past p is the oldest kept, once the window has moved past p.
                    wrong.fetch_add(1);
                }
            }
            Heard edge;
            if (at.frameAt(p, f) && at.hopAt(std::numeric_limits<int64_t>::min(), edge) && edge.position <= p) {
                // p was still inside the window after the read, so it was during it: the answer is the hop at or before p.
                // The expected hop at or before p, its phases moved on as frameAt moves them.
                const auto it = std::upper_bound(expected.begin(), expected.end(), p,
                                                 [](int64_t pos, const Heard& e) { return pos < e.position; });
                const Heard& e = it == expected.begin() ? expected.front() : *(it - 1);
                GeodeSoundFrame want = e.frame;
                want.fadeGain = 1.0f;
                const int64_t since = p > e.position ? (p - e.position) >> e.halvings : 0;
                Rhythm::phases(e.clock, static_cast<int>(since), want.hitTime, want);
                if (!same(f, want)) wrong.fetch_add(1);
            }
        }
    };
    // On the old edge: the oldest readable slot is the next but one the thread rewrites, and a visit of the whole
    // window crosses the slot being written, so these copies race the writer all the time. Each must be whole.
    const auto edgeReader = [&] {
        Heard h;
        while (writing.load(std::memory_order_acquire)) {
            if (at.hopAt(std::numeric_limits<int64_t>::min(), h)) {
                edgeReads.fetch_add(1, std::memory_order_relaxed);
                if (h.index >= expected.size() || !same(h, expected[h.index])) torn.fetch_add(1);
            }
            at.hopsBetween(std::numeric_limits<int64_t>::min(), kAll, [&](const Heard& v) {
                edgeReads.fetch_add(1, std::memory_order_relaxed);
                if (v.index >= expected.size() || !same(v, expected[v.index])) torn.fetch_add(1);
            });
        }
    };
    std::thread r1(reader, 93u);
    std::thread r2(reader, 141u);
    std::thread r3(edgeReader);
    const auto deadline = Clock::now() + std::chrono::seconds(60);
    Lcg rng{77u};
    int writes = 0;
    bool kept = true;
    for (int64_t done = 0; done < total && kept;) {
        kept = paced(ring, at, deadline);
        const int size = static_cast<int>(std::min<int64_t>(total - done, 1 + rng.below(1000)));
        ring.writeFloat(pcm.data() + 2 * done, size, 2, rate);
        at.notify();
        writes++;
        done += size;
    }
    check(kept, "the thread keeps up with the writer");
    while (at.newestPosition() < expected.back().position && Clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    writing.store(false, std::memory_order_release);
    r1.join();
    r2.join();
    r3.join();
    const uint64_t passes = at.passes();
    at.stop();

    int published = 0;
    int differ = 0;
    at.hopsBetween(-1, kAll, [&](const Heard& h) {
        published++;
        if (h.index >= expected.size() || !same(h, expected[h.index])) differ++;
    });
    std::printf("  threaded: %d writes, %llu passes, %d reads, %d on the old edge, %d torn, %d wrong; %d kept hops, %d differ\n",
                writes, static_cast<unsigned long long>(passes), reads.load(), edgeReads.load(), torn.load(), wrong.load(),
                published, differ);
    check(at.newestPosition() == expected.back().position, "the thread published every hop");
    check(published == at.published() - 1 && differ == 0, "the threaded hops equal the inline run's");
    check(torn.load() == 0, "a reader never sees a torn hop");
    check(wrong.load() == 0, "a reader reads the hop at its position, phases moved on");
    check(reads.load() > 0 && edgeReads.load() > 0, "the readers read while the writer wrote");
    check(passes <= static_cast<uint64_t>(2 * writes + 16), "the thread wakes about once per write: no polling");
}

// start, stop and start again: no thread left behind, a thread never started is fine, the destructor stops one.
void startStop() {
    const int rate = 44100;
    const std::vector<float> pcm = clicks(rate, 77.0, 3.0);
    PcmRing ring(kRingFrames, 2, kRunway);
    const int before = threadCount();
    { AnalysisThread never(ring); }
    {
        AnalysisThread at(ring, false);
        int64_t done = 0;
        for (int round = 0; round < 3; round++) {
            check(at.start() && at.running(), "the thread starts again");
            check(at.start(), "a second start while running is fine");
            const auto deadline = Clock::now() + std::chrono::seconds(30);
            for (int i = 0; i < 40; i++) {
                ring.writeFloat(pcm.data() + 2 * done, 1024, 2, rate);
                at.notify();
                done += 1024;
                paced(ring, at, deadline);
            }
            while (at.newestPosition() < done - 2048 && Clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(at.newestPosition() >= done - 2048, "the thread hears what was written");
            at.stop();
            check(!at.running(), "stop() joins");
            at.stop();
            if (before >= 0) check(threadsBackTo(before), "no thread left after stop()");
        }
        // Stopped, it pumps inline.
        const int64_t newest = at.newestPosition();
        ring.writeFloat(pcm.data() + 2 * done, 4096, 2, rate);
        at.pump(0.01f);
        check(at.newestPosition() > newest, "a stopped thread pumps inline");
        check(at.start(), "and starts once more");
    }
    if (before >= 0) check(threadsBackTo(before), "the destructor stops a running thread");
}

// The writer stops: the timed wait feeds the engine silence until it says so, then the thread sleeps untimed.
void writerStops() {
    const int rate = 48000;
    const std::vector<float> pcm = clicks(rate, 141.0, 2.0);
    const int64_t total = static_cast<int64_t>(pcm.size() / 2);
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, true);
    check(at.start(), "the thread starts");
    const auto deadline = Clock::now() + std::chrono::seconds(60);
    for (int64_t done = 0; done < total;) {
        paced(ring, at, deadline);
        const int size = static_cast<int>(std::min<int64_t>(total - done, 960));
        ring.writeFloat(pcm.data() + 2 * done, size, 2, rate);
        at.notify();
        done += size;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const int64_t end = ring.headFrames();
    while (at.newestPosition() + 2048 < end && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    GeodeSoundFrame playing{};
    at.frameAt(end, playing);
    // The starved hops all sit at the writer's last position, so they are told apart by index.
    bool silence = false;
    const int64_t from = at.newestPosition() - 1;
    uint64_t next = 0;
    while (Clock::now() < deadline && !(silence && at.idle())) {
        at.hopsBetween(from, kAll, [&](const Heard& h) {
            if (h.index < next) return;
            if (h.frame.silence > 0.0f) silence = true;
            next = h.index + 1;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    GeodeSoundFrame quiet{};
    at.frameAt(kAll, quiet);
    std::printf("  writer stops: silence %s, idle %s, rms %.4f then %.4f, level %.3f then %.3f\n", silence ? "fired" : "not fired",
                at.idle() ? "yes" : "no", playing.rms, quiet.rms, playing.level, quiet.level);
    check(silence, "the silence rule fires once the writer stops");
    check(at.idle(), "then the thread sleeps untimed");
    check(quiet.rms == 0.0f && quiet.rms < playing.rms && quiet.level <= playing.level, "the levels fall");
    const uint64_t passes = at.passes();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check(at.passes() == passes && at.idle(), "an idle thread does not wake");
    // Audio again wakes it.
    ring.writeFloat(pcm.data(), 4800, 2, rate);
    at.notify();
    while (at.newestPosition() + 2048 < ring.headFrames() && Clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(at.newestPosition() + 2048 >= ring.headFrames(), "audio after the silence wakes the thread");
    at.stop();
}

}  // namespace

// The listen limit holds audio back: the writer's wakes change nothing then (the limit moves with time), so the thread
// wakes about once a hop for the limit, not once more per write.
std::atomic<int64_t> gHeldLimit{kAll};
int64_t heldLimit(void*) { return gHeldLimit.load(); }

void heldBackSleepsThroughWrites() {
    const int rate = 48000;
    const std::vector<float> pcm = clicks(rate, 133.0, 2.0);
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, false);
    gHeldLimit.store(kAll);
    check(at.setListenLimit(heldLimit, nullptr) && at.start(), "the thread starts with a listen limit");
    ring.writeFloat(pcm.data(), 8192, 2, rate);
    at.ringWritten();
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (at.newestPosition() < 8192 - 2048 && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(at.newestPosition() >= 8192 - 2048, "it hears what the limit lets by");
    gHeldLimit.store(ring.headFrames());
    int64_t done = 8192;
    ring.writeFloat(pcm.data() + 2 * done, 256, 2, rate);
    done += 256;
    at.ringWritten();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const uint64_t before = at.passes();
    const int64_t newest = at.newestPosition();
    for (int i = 0; i < 40; i++) {
        ring.writeFloat(pcm.data() + 2 * done, 256, 2, rate);
        done += 256;
        at.ringWritten();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const uint64_t woke = at.passes() - before;
    std::printf("  held back: %llu passes over 40 writes\n", static_cast<unsigned long long>(woke));
    check(woke < 35, "audio the limit holds back wakes the thread no more than the limit's own wait does");
    check(at.newestPosition() == newest, "and none of it is heard");
    gHeldLimit.store(kAll);
    at.notify();
    while (at.newestPosition() < done - 2048 && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(at.newestPosition() >= done - 2048, "once the limit moves on, it hears all of it");
    at.stop();
}

// A sender holding the control lock never stalls a pass: the pass runs, the controls stay queued, and the next pass
// after the lock is free applies them, in the order sent.
static void controlLockNeverBlocksAPass() {
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, false);
    check(at.setTuning(SoundEngine::defaultTuning()) && at.trackBoundary() && at.reset(), "controls queue");
    check(AnalysisThreadTestAccess::waiting(at) == 3, "three wait");

    std::atomic<bool> held{false};
    std::atomic<bool> release{false};
    std::thread sender([&] {
        std::lock_guard<std::mutex> lock(AnalysisThreadTestAccess::lock(at));
        held.store(true);
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    while (!held.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    std::atomic<bool> passed{false};
    std::thread pass([&] {
        at.pump(0.01f);
        passed.store(true);
    });
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!passed.load() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(passed.load(), "a pass completes while another thread holds the control lock");
    release.store(true);
    sender.join();
    pass.join();
    check(AnalysisThreadTestAccess::waiting(at) == 3, "the held pass applied nothing and dropped nothing");

    at.pump(0.01f);
    check(AnalysisThreadTestAccess::waiting(at) == 0, "the next pass applies all three");
    check(at.setTuning(SoundEngine::defaultTuning()), "and the queue takes more");
}

GeodeSoundTuning gained(float gain) {
    GeodeSoundTuning t = SoundEngine::defaultTuning();
    t.gain[0] = gain;
    return t;
}

// The lock, held by another thread until let() releases it.
struct LockHolder {
    explicit LockHolder(AnalysisThread& at) {
        holder = std::thread([this, &at] {
            std::lock_guard<std::mutex> lock(AnalysisThreadTestAccess::lock(at));
            held.store(true);
            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
        while (!held.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void let() {
        release.store(true);
        holder.join();
    }
    std::atomic<bool> held{false};
    std::atomic<bool> release{false};
    std::thread holder;
};

// A pass that could not take the lock leaves the queue whole: the controls sent after it join the back, and the
// next pass applies all of them in the order sent (the last tuning wins).
void orderSurvivesAFailedPass() {
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, false);
    check(at.setTuning(gained(2.0f)), "tuning X queues");
    {
        LockHolder held(at);
        at.pump(0.01f);
        held.let();
    }
    check(AnalysisThreadTestAccess::waiting(at) == 1, "the failed pass left X queued");
    check(at.trackBoundary() && at.setTuning(gained(3.0f)), "boundary and tuning Y queue after the failed pass");
    check(AnalysisThreadTestAccess::waiting(at) == 3, "X, boundary, Y wait in that order");
    at.pump(0.01f);
    check(AnalysisThreadTestAccess::waiting(at) == 0, "the next pass applies all three");
    check(AnalysisThreadTestAccess::tuning(at).gain[0] == 3.0f, "the last tuning sent (Y) is the one applied");
}

// The thread's first pass fails on the held lock with a control waiting and no writer or sender left to notify:
// it must come back by itself once the lock is free.
void failedPassWakesTheThread() {
    PcmRing ring(kRingFrames, 2, kRunway);
    AnalysisThread at(ring, false);
    check(at.setTuning(gained(2.5f)), "tuning queues before the thread starts");
    LockHolder held(at);
    check(at.start(), "thread starts");
    const auto spun = Clock::now() + std::chrono::seconds(10);
    while (at.passes() == 0 && Clock::now() < spun) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    held.let();
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (AnalysisThreadTestAccess::waiting(at) != 0 && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(AnalysisThreadTestAccess::waiting(at) == 0, "the thread applies the control with no write and no notify");
    at.stop();
    check(AnalysisThreadTestAccess::tuning(at).gain[0] == 2.5f, "and it applied the tuning queued");
}

// The mic and another app's audio are heard through the engine's automatic input gain, the player as it is: a
// 220 Hz tone at -66 dBFS (under the -55 LUFS gate, over the gain's floor) opens the gate within 4 s when the
// source is not the player, and never while it is.
void quietSourceIsGained() {
    for (bool player : {false, true}) {
        const int rate = 48000;
        PcmRing ring(kRingFrames, 2, kRunway);
        AnalysisThread at(ring, false);
        at.setPlayerSource(player);
        std::vector<float> block(2 * 960);
        int64_t written = 0;
        bool opened = false;
        for (int i = 0; i < 4 * 50; i++) {
            for (size_t s = 0; s < block.size() / 2; s++) {
                const double t = static_cast<double>(written + static_cast<int64_t>(s)) / rate;
                block[2 * s] = block[2 * s + 1] = static_cast<float>(std::pow(10.0, -66.0 / 20.0) * std::sin(kTau * 220.0 * t));
            }
            ring.writeFloat(block.data(), static_cast<int>(block.size() / 2), 2, rate);
            written += static_cast<int64_t>(block.size() / 2);
            at.pump(0.02f);
            GeodeSoundFrame f{};
            if (at.frameAt(kAll, f) && f.loudnessM > 0.0f) opened = true;
        }
        std::printf("  a -66 dBFS tone from %s: the gate %s\n", player ? "the player" : "another source", opened ? "opened" : "stayed shut");
        check(opened != player, player ? "the player's quiet track was gained" : "a quiet source other than the player was not gained");
    }
}

int main() {
    std::printf("analysis thread: one engine off the draw thread, hops by ring position\n");
    quietSourceIsGained();
    for (int rate : {48000, 96000}) inlineMatchesBare(rate, false);
    inlineMatchesBare(48000, true);
    inlineSkipAndRate();
    threadedMatchesInline();
    startStop();
    writerStops();
    heldBackSleepsThroughWrites();
    controlLockNeverBlocksAPass();
    orderSurvivesAFailedPass();
    failedPassWakesTheThread();
    if (failures.load() == 0) std::printf("analysis_thread_test: all checks passed\n");
    return failures.load() == 0 ? 0 : 1;
}
