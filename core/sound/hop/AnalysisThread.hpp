#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

#include "audio/ingest/PcmRing.hpp"
#include "sound/AnalysisSettings.hpp"
#include "sound/Emotion.hpp"
#include "sound/Rhythm.hpp"
#include "sound/SoundEngine.hpp"
#include "sound/SoundFrame.h"

namespace geode::sound {

// One sound engine fed from one PCM ring on a thread of its own, its hops published by ring position for any
// number of reader threads (#1215).
//
// The thread sleeps until there is something to do: the ring's writer calls notify() after each write, a
// control call wakes it, stop() wakes it. It takes everything the cursor has, writes it to the engine and
// publishes every hop the engine ran. Readers never touch the engine (it is single-threaded): each hop is
// copied into a ring of seqlocked slots, and a reader copies a slot out and moves the phases on itself, as
// SoundEngine::frameAt does. No lock and no allocation on the reader's path or the writer's notify().
//
// Positions are the ring's (PcmRing::headFrames(), never reset), while the engine's restart at every reset;
// the thread keeps the difference, so a published hop's position is the ring frame its last sample came from.
//
// Inline mode: never start() it and call pump() instead, one pass on the caller's thread with the same
// publishing, for an export and for deterministic tests. start(), stop() and pump() belong to one owner thread.
class AnalysisThread {
public:
    // The exclusive ring position the engine may hear up to now (sound/interp/listenLimit), called on the
    // analysis thread at each pass with the context given; null reads up to the newest frame.
    using ListenLimit = int64_t (*)(void* context);

    // One published hop: what SoundEngine::Hop holds, placed by ring position.
    struct Heard {
        uint64_t index = 0;    // hops published before this one
        int64_t position = 0;  // the ring frame just past the hop's last sample (headFrames() units)
        Rhythm::Clock clock;   // the oscillator the phases move on from
        int halvings = 0;      // a position difference >> halvings is analysis samples
        GeodeSoundFrame frame{};  // the hop's own: phases at its sample, its own triggers, no waveform
        Emotion::State emotion{};
    };
    static_assert(std::is_trivially_copyable_v<Heard>);

    // Published hops kept: at least this long at every rate, so the shortest hop (the highest analysis rate,
    // under 88.2 kHz) sizes the slots. One fewer can be read; the oldest slot is the one written next.
    static constexpr float kPublishedSeconds = 2.5f;

    // The ring must outlive this. silenceWhenStarved feeds the engine silence once its writer stops (step()'s
    // rule), off for a run that must not depend on wall time. Every engine it makes runs on `settings`.
    explicit AnalysisThread(const audio::ingest::PcmRing& ring, bool silenceWhenStarved = true, AnalysisSettings settings = {});
    // The published slots, from kPublishedSeconds and the settings' hop.
    int published() const { return static_cast<int>(capacity_); }
    ~AnalysisThread();
    AnalysisThread(const AnalysisThread&) = delete;
    AnalysisThread& operator=(const AnalysisThread&) = delete;

    // False when no thread could be made. Again while running does nothing.
    bool start();
    // Wakes the thread and joins it; the engine and everything published stay. Safe when never started.
    void stop();
    bool running() const { return thread_.joinable(); }

    // Inline: one pass as the thread makes it, dtSeconds since the last for the starved engine's silence.
    // Not while started.
    void pump(float dtSeconds);

    // The ring's writer calls this after each write: never blocks, takes no lock. The shell calls it too when a listen
    // limit moves on with no write (playback resumed with the sink's buffer already in the ring): once the audio held
    // back has waited a second, the thread sleeps until a notify.
    void notify();
    // What a pass that wrote audio reports on the analysis thread: its work in nanoseconds against the nanoseconds of
    // audio it covered (ADPF, which the layer above hands in; null reports nothing). Process-wide; set before start().
    using WorkReporter = void (*)(int64_t targetNs, int64_t actualNs);
    static void setWorkReporter(WorkReporter reporter);
    // The ring's writer calls this, not notify(), after each write: the same, except that a thread asleep for the
    // listen limit to move on is not woken by audio the limit holds back anyway (the limit moves it, with time).
    void ringWritten();

    // Controls, applied by the analysis thread at its next pass before it takes more audio, in the order sent.
    // False when the queue is full (32 waiting), and nothing is applied.
    bool setTuning(const GeodeSoundTuning& tuning);
    // The player's track boundary. The ring's new epoch at a gapless join (the tap's flush) already started the engine
    // over on the join's first sample, before the player names it: a boundary that soon after a restart is that join's
    // and changes nothing, or it would start the new song over a second time, a sink's lead late.
    bool trackBoundary();
    // The engine starts over (a new source, a seek the shell names); the cursor keeps its place.
    bool reset();
    // A new play: as trackBoundary, nothing when the engine (its feeling too) just started over at a join.
    bool resetEmotion();
    // `context` must outlive stop() (or the next setListenLimit that replaces it): the thread calls `limit` with it.
    bool setListenLimit(ListenLimit limit, void* context);
    // The latest value wins; applied at the next pass.
    void setFadeGain(float gain);
    void setPlayerSource(bool on);

    // Readers, any thread. The hop at or before `position`, as SoundEngine::frameAt picks it (older than every
    // kept one reads the oldest, past the newest the newest), its phases moved on to `position`, fadeGain as
    // set now. False before the first hop, and when the writer lapped this reader on every try (it stalled for about
    // a whole window): no hop it read can be trusted then.
    bool frameAt(int64_t position, GeodeSoundFrame& out, Emotion::State* emotion = nullptr) const;
    // The same pick, as published.
    bool hopAt(int64_t position, Heard& out) const;
    // The oldest readable hop past `position`; false when none is published yet (or the reader was lapped).
    bool hopAfter(int64_t position, Heard& out) const;
    // The newest hop's position, -1 before the first.
    int64_t newestPosition() const;
    // Every readable hop with from < position <= to, oldest first; a hop the writer lapped during the
    // visit is skipped. Returns the hops visited.
    template <typename Visit>
    int hopsBetween(int64_t from, int64_t to, Visit&& visit) const {
        const uint64_t count = published_.load(std::memory_order_acquire);
        Heard h;
        int visited = 0;
        for (uint64_t i = firstAfter(from, count); i < count; ++i) {
            if (!read(i, h) || h.position <= from) continue;
            if (h.position > to) break;
            visit(static_cast<const Heard&>(h));
            ++visited;
        }
        return visited;
    }

    // Diagnostics: the thread's passes, and whether it sleeps with nothing to do (no timeout pending).
    uint64_t passes() const { return passes_.load(std::memory_order_relaxed); }
    bool idle() const { return idle_.load(std::memory_order_acquire); }

private:
    struct Control {
        enum Kind : uint8_t { kTuning, kBoundary, kReset, kEmotionReset, kLimit } kind = kBoundary;
        GeodeSoundTuning tuning{};
        ListenLimit limit = nullptr;
        void* context = nullptr;
    };
    static constexpr int kControls = 32;

    enum class Next { kNow, kWait, kWaitTimed, kWaitLimit };

    static_assert(sizeof(Heard) % sizeof(uint32_t) == 0);
    static constexpr int kWords = static_cast<int>(sizeof(Heard) / sizeof(uint32_t));
    // The frame's waveform is zero in every published hop (step() draws it, a hop does not): its words are neither
    // stored nor loaded, and a read zeroes them.
    static constexpr int kWaveFirst = static_cast<int>((offsetof(Heard, frame) + offsetof(GeodeSoundFrame, waveform)) / sizeof(uint32_t));
    static constexpr int kWaveEnd = kWaveFirst + GEODE_SOUND_WAVEFORM_POINTS;
    // A seqlocked copy of one Heard: seq is odd while the analysis thread writes it. The words are atomics so
    // a reader's copy that races the writer is defined (and thrown away); position is the search's hint.
    struct Slot {
        std::atomic<uint32_t> seq{0};
        std::atomic<int64_t> position{0};
        std::array<std::atomic<uint32_t>, kWords> words{};
    };

    void run();
    Next pass(float dtSeconds);
    bool send(const Control& control);
    void applyControls();
    Next waitUnlessDeferred() const;
    bool ensureEngine(int sampleRateHz);
    void restartEngine();
    bool startedOverForBoundary() const;
    void write(const float* interleaved, int frames, int channels);
    void publishNew();
    void publish(const SoundEngine::Hop& hop);
    // run()'s: one pass's work against the audio it covered (ADPF).
    void reportWork(std::chrono::steady_clock::time_point started);
    void timedWait(uint32_t seen, std::chrono::microseconds timeout);

    bool read(uint64_t index, Heard& out) const;
    uint64_t firstAfter(int64_t from, uint64_t count) const;
    float fadeNow() const;

    const audio::ingest::PcmRing& ring_;
    const bool silenceWhenStarved_;
    const AnalysisSettings settings_;
    const uint64_t capacity_;  // published slots
    const uint64_t readable_;  // capacity_ - 1

    // The analysis thread's own (or pump()'s caller's).
    audio::ingest::Cursor cursor_;
    std::unique_ptr<SoundEngine> engine_;
    GeodeSoundTuning tuning_{};
    bool tuned_ = false;
    float fadeApplied_ = 1.0f;
    bool playerApplied_ = false;
    ListenLimit limit_ = nullptr;
    void* limitContext_ = nullptr;
    int64_t engineWritten_ = 0;  // input frames written to the engine since its construction or reset
    int64_t offset_ = 0;         // ring position minus engine position
    int64_t sinceRestart_ = -1;  // input frames written since the engine last started over at a restart, -1 before one
    int64_t hopsSeen_ = 0;       // engine_->hopsRun() as last published
    bool flowing_ = false;       // audio came, and the engine has not reported silence since
    float quietSeconds_ = 0.0f;  // since audio last came, while flowing
    std::array<Control, kControls> applying_{};
    bool controlsDeferred_ = false;  // the last applyControls() could not take the lock
    std::array<uint32_t, kWords> staging_{};

    // Shared with the control senders. The analysis thread only try-locks it (applyControls); senders may block.
    friend struct AnalysisThreadTestAccess;
    std::mutex controlLock_;
    std::array<Control, kControls> controls_{};
    int controlCount_ = 0;
    std::atomic<float> fade_{1.0f};
    std::atomic<bool> player_{false};

    // Shared with the readers.
    std::unique_ptr<Slot[]> slots_;
    std::atomic<uint64_t> published_{0};

    // The wake.
    std::atomic<uint32_t> signal_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> timedWaiting_{false};
    // In the timed wait for the listen limit to move on (set around it by the analysis thread).
    std::atomic<bool> limitWaiting_{false};
    // The analysis thread's: how long that wait is, set by the pass that asks for it.
    std::chrono::microseconds limitWait_{10000};
    // Set before the untimed wait on signal_ and cleared after it, so notify() wakes only a thread that may sleep.
    std::atomic<bool> waiting_{false};
    std::mutex waitLock_;
    std::condition_variable waitCv_;
    std::atomic<uint64_t> passes_{0};
    int64_t workFrames_ = 0;      // input frames the last pass wrote to the engine
    int workRateHz_ = 0;          // their rate
    std::atomic<bool> idle_{false};
    std::thread thread_;
};

}  // namespace geode::sound
