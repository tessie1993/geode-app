#include "sound/hop/AnalysisThread.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#ifdef __ANDROID__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace geode::sound {

namespace {

// Takes per pass before the thread looks at stop and the controls again.
constexpr int kTakesPerPass = 16;
// One write to the engine runs at most this share of the hops it keeps, so none is gone before it is published.
constexpr int kChunkShare = 4;
// The highest analysis rate: SoundEngine::analysisRateFor halves from here.
constexpr double kTopAnalysisRate = 88200.0;
// While audio is flowing the wait has this timeout, so a writer that stops still lets step() feed silence.
constexpr std::chrono::microseconds kStarvedTick{50000};
// While a listen limit holds back audio already in the ring: the limit moves with time, not with writes, so the wait is
// the time it takes to move on by one hop (at least this long, the hop's own length when the engine is known).
constexpr std::chrono::microseconds kLimitTick{10000};
// The timed waits end at this much quiet even if the engine never reports silence, and sooner while a listen limit
// is all that holds audio back (a pause with the sink's buffer still unplayed): then only notify() resumes it.
constexpr float kFlowCapSeconds = 5.0f;
constexpr float kHeldBackCapSeconds = 1.0f;
// step()'s own clamp on dt, so the quiet counted here is the quiet the engine counts.
constexpr float kMaxStepSeconds = 0.25f;
// A reader's tries at one slot, and at the whole search, before it falls back.
constexpr int kReadTries = 64;
constexpr int kLapTries = 4;

#ifdef __ANDROID__
// One step below the stage thread's nice -4 (StageThread.cpp), still above a default thread: the analysis feeds the
// picture's next frame, so it is never starved by app threads, and never outranks the thread that draws.
constexpr int kAnalysisNice = -2;
#endif

float clampFade(float gain) { return std::isfinite(gain) ? std::clamp(gain, 0.0f, 1.0f) : 1.0f; }

// The published words, the hints and the counters must be plain loads and stores on every target.
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<int64_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);

}  // namespace

AnalysisThread::AnalysisThread(const audio::ingest::PcmRing& ring, bool silenceWhenStarved, AnalysisSettings settings)
    : ring_(ring),
      silenceWhenStarved_(silenceWhenStarved),
      settings_(settings.checked()),
      capacity_(static_cast<uint64_t>(
          std::max(2.0, std::ceil(kPublishedSeconds * kTopAnalysisRate / static_cast<double>(settings_.hopSamples()))))),
      readable_(capacity_ - 1),
      cursor_(ring),
      slots_(std::make_unique<Slot[]>(capacity_)) {}

AnalysisThread::~AnalysisThread() { stop(); }

bool AnalysisThread::start() {
    if (thread_.joinable()) return true;
    stopping_.store(false, std::memory_order_seq_cst);
    try {
        thread_ = std::thread(&AnalysisThread::run, this);
    } catch (...) {
        return false;
    }
    return true;
}

void AnalysisThread::stop() {
    if (!thread_.joinable()) return;
    stopping_.store(true, std::memory_order_seq_cst);
    signal_.fetch_add(1, std::memory_order_seq_cst);
    signal_.notify_one();
    // Through the lock: the thread is either before its timed wait's check, which sees the signal, or inside the
    // wait, where this notify reaches it.
    { std::lock_guard<std::mutex> lock(waitLock_); }
    waitCv_.notify_one();
    thread_.join();
    idle_.store(false, std::memory_order_release);
}

// No lost wake (every access below and in run()'s untimed wait is seq_cst, so they fall in one order): if this
// load of waiting_ misses the flag, it comes before the thread's store of it, so the thread's wait then compares
// against the signal this call already moved and returns at once. If it sees the flag, the notify follows the
// add, which reaches a wait that compared against an older value.
void AnalysisThread::notify() {
    signal_.fetch_add(1, std::memory_order_seq_cst);
    if (waiting_.load(std::memory_order_seq_cst)) signal_.notify_one();
    if (timedWaiting_.load(std::memory_order_seq_cst)) waitCv_.notify_one();
}

// ADPF: a pass that wrote audio to the engine had that audio's own length to finish in.
void AnalysisThread::reportWork(std::chrono::steady_clock::time_point started) {
    if (workFrames_ <= 0 || workRateHz_ <= 0) return;
    const int64_t audioNs = workFrames_ * 1000000000 / workRateHz_;
    workHint_.report(audioNs, std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
}

// A write the listen limit holds back changes nothing until the limit moves, which only time does: while this thread
// sleeps on that, the write bumps the signal (so no pass is missed) but does not wake it. Everything else wakes it.
void AnalysisThread::ringWritten() {
    signal_.fetch_add(1, std::memory_order_seq_cst);
    if (waiting_.load(std::memory_order_seq_cst)) signal_.notify_one();
    if (timedWaiting_.load(std::memory_order_seq_cst) && !limitWaiting_.load(std::memory_order_seq_cst)) waitCv_.notify_one();
}

void AnalysisThread::run() {
#ifdef __ANDROID__
    // Best effort: a refusal leaves the inherited priority. The work done per pass is unchanged.
    setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), kAnalysisNice);
#endif
    auto last = std::chrono::steady_clock::now();
    for (;;) {
        // The signal before the work: a notify after this load changes it, so the wait below returns at once.
        const uint32_t seen = signal_.load(std::memory_order_seq_cst);
        if (stopping_.load(std::memory_order_seq_cst)) return;
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        passes_.fetch_add(1, std::memory_order_relaxed);
        const Next next = pass(dt);
        reportWork(now);
        switch (next) {
            case Next::kNow:
                break;
            case Next::kWaitTimed:
                timedWait(seen, kStarvedTick);
                break;
            case Next::kWaitLimit:
                limitWaiting_.store(true, std::memory_order_seq_cst);
                timedWait(seen, limitWait_);
                limitWaiting_.store(false, std::memory_order_seq_cst);
                break;
            case Next::kWait:
                idle_.store(true, std::memory_order_release);
                waiting_.store(true, std::memory_order_seq_cst);
                signal_.wait(seen, std::memory_order_seq_cst);
                waiting_.store(false, std::memory_order_seq_cst);
                idle_.store(false, std::memory_order_release);
                break;
        }
    }
}

// A notify that lands between the predicate's check and the wait's sleep is late by at most `timeout`, never lost:
// the writer cannot take waitLock_, so this is the one wait with a bound instead of a guarantee.
void AnalysisThread::timedWait(uint32_t seen, std::chrono::microseconds timeout) {
    std::unique_lock<std::mutex> lock(waitLock_);
    timedWaiting_.store(true, std::memory_order_seq_cst);
    waitCv_.wait_for(lock, timeout, [&] { return signal_.load(std::memory_order_seq_cst) != seen; });
    timedWaiting_.store(false, std::memory_order_relaxed);
}

void AnalysisThread::pump(float dtSeconds) {
    if (thread_.joinable()) return;
    passes_.fetch_add(1, std::memory_order_relaxed);
    for (float dt = dtSeconds; pass(dt) == Next::kNow; dt = 0.0f) {
    }
}

AnalysisThread::Next AnalysisThread::pass(float dtSeconds) {
    applyControls();
    const int64_t limit = limit_ ? limit_(limitContext_) : audio::ingest::Cursor::kNewest;
    bool heard = false;
    bool more = false;
    workFrames_ = 0;
    for (int takes = 0; takes < kTakesPerPass; ++takes) {
        const audio::ingest::Cursor::Take take = cursor_.take(limit);
        if (take.restarted) restartEngine();
        if (take.frames <= 0) break;
        if (!ensureEngine(take.sampleRateHz)) break;
        // Per take, so a skip or a restart re-bases it.
        offset_ = cursor_.position() - take.frames - engineWritten_;
        write(cursor_.interleaved(), take.frames, cursor_.channels());
        workFrames_ += take.frames;
        workRateHz_ = take.sampleRateHz;
        heard = true;
        more = (takes + 1 == kTakesPerPass);
    }
    // Audio in the ring that only the listen limit holds back: the writer has not stopped, the speaker has.
    const bool heldBack = limit_ && cursor_.position() < ring_.headFrames();
    if (heard) {
        flowing_ = true;
        quietSeconds_ = 0.0f;
    }
    if (engine_ && silenceWhenStarved_ && flowing_ && !heldBack) {
        // starve() is what feeds a starved engine silence (> 0.25 s without audio), step()'s clock without its frame.
        // Only from audio until the engine says silence, so an idle thread woken by a control publishes nothing.
        engine_->starve(dtSeconds);
        publishNew();
    }
    // The timed waits run only from audio until the engine says silence (or a cap), then the thread sleeps untimed.
    if (!heard && flowing_) {
        quietSeconds_ += std::clamp(dtSeconds, 0.0f, kMaxStepSeconds);
        const float cap = heldBack ? kHeldBackCapSeconds : kFlowCapSeconds;
        if (!engine_ || (!heldBack && engine_->silent()) || quietSeconds_ > cap) flowing_ = false;
    }
    if (more) return Next::kNow;
    if (!flowing_) return Next::kWait;
    if (heldBack) {
        limitWait_ = kLimitTick;
        if (engine_) {
            // One hop of the engine's input frames at the input rate: what the limit takes to admit the next hop.
            const int64_t hopFrames = static_cast<int64_t>(engine_->hopSamples()) << engine_->halvings();
            const int64_t micros = hopFrames * 1000000 / std::max(1, engine_->sampleRateHz());
            limitWait_ = std::chrono::microseconds(std::max<int64_t>(kLimitTick.count(), micros));
        }
        return Next::kWaitLimit;
    }
    return silenceWhenStarved_ ? Next::kWaitTimed : Next::kWait;
}

bool AnalysisThread::send(const Control& control) {
    {
        std::lock_guard<std::mutex> lock(controlLock_);
        if (controlCount_ == kControls) return false;
        controls_[static_cast<size_t>(controlCount_++)] = control;
    }
    notify();
    return true;
}

bool AnalysisThread::setTuning(const GeodeSoundTuning& tuning) {
    Control c;
    c.kind = Control::kTuning;
    c.tuning = tuning;
    return send(c);
}

bool AnalysisThread::trackBoundary() {
    Control c;
    c.kind = Control::kBoundary;
    return send(c);
}

bool AnalysisThread::reset() {
    Control c;
    c.kind = Control::kReset;
    return send(c);
}

bool AnalysisThread::resetEmotion() {
    Control c;
    c.kind = Control::kEmotionReset;
    return send(c);
}

bool AnalysisThread::setListenLimit(ListenLimit limit, void* context) {
    Control c;
    c.kind = Control::kLimit;
    c.limit = limit;
    c.context = context;
    return send(c);
}

void AnalysisThread::setFadeGain(float gain) { fade_.store(gain, std::memory_order_relaxed); }

void AnalysisThread::setPlayerSource(bool on) { player_.store(on, std::memory_order_relaxed); }

void AnalysisThread::applyControls() {
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(controlLock_);
        count = controlCount_;
        std::copy_n(controls_.begin(), count, applying_.begin());
        controlCount_ = 0;
    }
    for (int i = 0; i < count; ++i) {
        const Control& c = applying_[static_cast<size_t>(i)];
        switch (c.kind) {
            case Control::kTuning:
                tuning_ = c.tuning;
                tuned_ = true;
                if (engine_) engine_->setTuning(tuning_);
                break;
            case Control::kBoundary:
                if (engine_) engine_->trackBoundary();
                break;
            case Control::kReset:
                restartEngine();
                break;
            case Control::kEmotionReset:
                if (engine_) engine_->resetEmotion();
                break;
            case Control::kLimit:
                limit_ = c.limit;
                limitContext_ = c.context;
                break;
        }
    }
    fadeApplied_ = fade_.load(std::memory_order_relaxed);
    playerApplied_ = player_.load(std::memory_order_relaxed);
    if (engine_) {
        engine_->setFadeGain(fadeApplied_);
        engine_->setPlayerSource(playerApplied_);
    }
}

void AnalysisThread::restartEngine() {
    if (!engine_) return;
    engine_->reset();
    engineWritten_ = 0;
    hopsSeen_ = 0;
    // Until the next take re-bases it: a starved engine's silence hops sit where the cursor stands.
    offset_ = cursor_.position();
}

bool AnalysisThread::ensureEngine(int sampleRateHz) {
    // SoundEngine clamps its rate to 8 kHz: compare the clamped rate, or a lower one rebuilds every take.
    if (engine_ && engine_->sampleRateHz() == std::max(8000, sampleRateHz)) return true;
    try {
        engine_ = std::make_unique<SoundEngine>(sampleRateHz, settings_);
    } catch (...) {
        // No memory: this take is lost, the next one tries again.
        engine_.reset();
        return false;
    }
    if (tuned_) engine_->setTuning(tuning_);
    engine_->setPlayerSource(playerApplied_);
    engine_->setFadeGain(fadeApplied_);
    engineWritten_ = 0;
    hopsSeen_ = 0;
    return true;
}

void AnalysisThread::write(const float* interleaved, int frames, int channels) {
    const int chunkHops = std::max(1, engine_->historyHops() / kChunkShare);
    const int chunk = std::max(1, (engine_->hopSamples() << engine_->halvings()) * chunkHops);
    for (int done = 0; done < frames;) {
        const int n = std::min(frames - done, chunk);
        engine_->write(interleaved + static_cast<size_t>(done) * static_cast<size_t>(channels), n, channels);
        engineWritten_ += n;
        done += n;
        publishNew();
    }
}

void AnalysisThread::publishNew() {
    const int64_t run = engine_->hopsRun();
    const int64_t fresh = std::min<int64_t>(run - hopsSeen_, engine_->historyHops());
    for (int64_t age = fresh - 1; age >= 0; --age) publish(*engine_->keptHop(static_cast<int>(age)));
    hopsSeen_ = run;
}

void AnalysisThread::publish(const SoundEngine::Hop& hop) {
    const uint64_t index = published_.load(std::memory_order_relaxed);
    // Zeroed first and the clock copied field by field, so no padding byte of the published copy is left undefined.
    Heard h;
    std::memset(static_cast<void*>(&h), 0, sizeof h);
    h.index = index;
    h.position = hop.sample + offset_;
    h.clock.theta = hop.clock.theta;
    h.clock.period = hop.clock.period;
    h.clock.lastBeat = hop.clock.lastBeat;
    h.clock.hop = hop.clock.hop;
    h.clock.meter = hop.clock.meter;
    h.clock.downSlot = hop.clock.downSlot;
    h.clock.barInPhrase = hop.clock.barInPhrase;
    h.clock.locked = hop.clock.locked;
    h.halvings = engine_->halvings();
    h.frame = hop.frame;
    h.frame.fadeGain = playerApplied_ ? clampFade(fadeApplied_) : 1.0f;
    h.emotion = hop.emotion;
    std::memcpy(staging_.data(), &h, sizeof h);
    Slot& slot = slots_[index % capacity_];
    const uint32_t seq = slot.seq.load(std::memory_order_relaxed);
    slot.seq.store(seq + 1, std::memory_order_relaxed);
    // Release on every store after the odd seq: a reader that loads any of them (acquire) then sees seq odd or
    // newer, so its check fails. No standalone fence, which ThreadSanitizer cannot follow.
    slot.position.store(h.position, std::memory_order_release);
    for (int i = 0; i < kWords; ++i) {
        if (i >= kWaveFirst && i < kWaveEnd) continue;
        slot.words[static_cast<size_t>(i)].store(staging_[static_cast<size_t>(i)], std::memory_order_release);
    }
    slot.seq.store(seq + 2, std::memory_order_release);
    published_.store(index + 1, std::memory_order_release);
}

bool AnalysisThread::read(uint64_t index, Heard& out) const {
    const Slot& slot = slots_[index % capacity_];
    auto* bytes = reinterpret_cast<unsigned char*>(&out);
    for (int attempt = 0; attempt < kReadTries; ++attempt) {
        const uint32_t before = slot.seq.load(std::memory_order_acquire);
        if ((before & 1u) != 0) continue;
        for (int i = 0; i < kWords; ++i) {
            if (i >= kWaveFirst && i < kWaveEnd) continue;
            const uint32_t word = slot.words[static_cast<size_t>(i)].load(std::memory_order_acquire);
            std::memcpy(bytes + static_cast<size_t>(i) * sizeof word, &word, sizeof word);
        }
        std::memset(bytes + static_cast<size_t>(kWaveFirst) * sizeof(uint32_t), 0, GEODE_SOUND_WAVEFORM_POINTS * sizeof(uint32_t));
        // After the acquire loads above, so it cannot be read before them.
        if (slot.seq.load(std::memory_order_relaxed) != before) continue;
        // A whole copy, but of a later lap when the writer passed this slot again.
        return out.index == index;
    }
    return false;
}

uint64_t AnalysisThread::firstAfter(int64_t from, uint64_t count) const {
    const uint64_t oldest = count > readable_ ? count - readable_ : 0;
    uint64_t i = count;
    while (i > oldest && slots_[(i - 1) % capacity_].position.load(std::memory_order_acquire) > from) --i;
    return i;
}

bool AnalysisThread::hopAt(int64_t position, Heard& out) const {
    for (int attempt = 0; attempt < kLapTries; ++attempt) {
        const uint64_t count = published_.load(std::memory_order_acquire);
        if (count == 0) return false;
        const uint64_t oldest = count > readable_ ? count - readable_ : 0;
        const uint64_t after = firstAfter(position, count);
        const uint64_t pick = after > oldest ? after - 1 : oldest;
        if (!read(pick, out)) continue;
        // The search passed over the slots of pick + 1 … count - 1 by their positions. The writer starts on a slot's
        // next lap only once published_ reaches pick + 1 + capacity_, so below that every position it read was the
        // hop it stood for, and pick is the newest at or before `position`.
        if (published_.load(std::memory_order_acquire) <= pick + capacity_) return true;
    }
    // Lapped on every try: the reader stalled for about a whole window. No hop it could trust.
    return false;
}

bool AnalysisThread::hopAfter(int64_t position, Heard& out) const {
    for (int attempt = 0; attempt < kLapTries; ++attempt) {
        const uint64_t count = published_.load(std::memory_order_acquire);
        const uint64_t after = firstAfter(position, count);
        if (after >= count) return false;
        if (!read(after, out) || out.position <= position) continue;
        // As hopAt: the slots the search read (after - 1 … count - 1) were the hops they stood for.
        if (published_.load(std::memory_order_acquire) < after + capacity_) return true;
    }
    return false;
}

bool AnalysisThread::frameAt(int64_t position, GeodeSoundFrame& out, Emotion::State* emotion) const {
    Heard h;
    if (!hopAt(position, h)) return false;
    out = h.frame;
    out.fadeGain = fadeNow();
    // As SoundEngine::frameAt: the analysis samples since the hop, the input's halvings divided out.
    const int64_t since = position > h.position ? (position - h.position) >> h.halvings : 0;
    Rhythm::phases(h.clock, static_cast<int>(std::min<int64_t>(since, std::numeric_limits<int>::max())), out.hitTime, out);
    if (emotion != nullptr) *emotion = h.emotion;
    return true;
}

int64_t AnalysisThread::newestPosition() const {
    const uint64_t count = published_.load(std::memory_order_acquire);
    if (count == 0) return -1;
    return slots_[(count - 1) % capacity_].position.load(std::memory_order_acquire);
}

float AnalysisThread::fadeNow() const {
    return player_.load(std::memory_order_relaxed) ? clampFade(fade_.load(std::memory_order_relaxed)) : 1.0f;
}

}  // namespace geode::sound
