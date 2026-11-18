#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

#include "audio/ingest/PcmRing.hpp"
#include "sound/hop/AnalysisThread.hpp"

namespace geode::sound {

// One ring's analysis, shared by every surface that reads the ring (#1215): kept beside the ring's handle
// (geode_pcm_ring::analysis), its thread running while at least one surface listens. Every write to the ring
// wakes it (PcmRing::setListener), on the writer's thread. It reads up to the newest frame written, ahead of the
// speaker, so the hops around the sample a surface draws are there when it asks (sound/interp/FrameBlend).
//
// Also a renderer's own analysis of audio only it hears (an export, inline by pump(); a live feed with the mic
// layered in, on its thread).
//
// Keeps the ring alive; the ring's writers must be done before the last owner lets go of this.
class SharedAnalysis final : public audio::ingest::RingListener {
public:
    // silenceWhenStarved as AnalysisThread takes it: off only for a run that must not depend on wall time.
    explicit SharedAnalysis(std::shared_ptr<const audio::ingest::PcmRing> ring, bool silenceWhenStarved = true)
        : ring_(std::move(ring)), thread_(*ring_, silenceWhenStarved) {
        ring_->setListener(this);
    }
    ~SharedAnalysis() {
        ring_->setListener(nullptr);
        thread_.stop();
    }
    SharedAnalysis(const SharedAnalysis&) = delete;
    SharedAnalysis& operator=(const SharedAnalysis&) = delete;

    void ringWritten() override { thread_.ringWritten(); }

    AnalysisThread& thread() { return thread_; }
    const AnalysisThread& thread() const { return thread_; }
    const audio::ingest::PcmRing& ring() const { return *ring_; }
    bool hears(const audio::ingest::PcmRing& ring) const { return ring_.get() == &ring; }

    // A surface listening: the first starts the thread, the last one gone stops it. Movable, one per surface.
    class Listening {
    public:
        Listening() = default;
        explicit Listening(std::shared_ptr<SharedAnalysis> shared) : shared_(std::move(shared)) {
            if (shared_) shared_->attach();
        }
        Listening(Listening&& other) noexcept : shared_(std::move(other.shared_)) {}
        Listening& operator=(Listening&& other) noexcept {
            if (this != &other) {
                release();
                shared_ = std::move(other.shared_);
            }
            return *this;
        }
        Listening(const Listening&) = delete;
        Listening& operator=(const Listening&) = delete;
        ~Listening() { release(); }
        const std::shared_ptr<SharedAnalysis>& shared() const { return shared_; }

    private:
        void release() {
            if (shared_) shared_->detach();
            shared_.reset();
        }
        std::shared_ptr<SharedAnalysis> shared_;
    };

    // The player's track boundary `epoch`, sent to the engine once, by whichever surface sees it first.
    void trackBoundary(uint64_t epoch) {
        uint64_t seen = boundary_.load(std::memory_order_relaxed);
        while (seen < epoch) {
            if (boundary_.compare_exchange_weak(seen, epoch, std::memory_order_relaxed)) {
                thread_.trackBoundary();
                return;
            }
        }
    }

    // A new play (the director's seed changed): the feeling starts over once, whichever surface sees it first.
    void newPlay(uint64_t seed) {
        if (play_.exchange(seed, std::memory_order_relaxed) != seed) thread_.resetEmotion();
    }

private:
    void attach() {
        std::lock_guard<std::mutex> lock(lock_);
        if (listeners_++ == 0) thread_.start();
    }
    void detach() {
        std::lock_guard<std::mutex> lock(lock_);
        if (--listeners_ == 0) thread_.stop();
    }

    std::shared_ptr<const audio::ingest::PcmRing> ring_;  // first: the thread refers to it
    AnalysisThread thread_;
    std::mutex lock_;
    int listeners_ = 0;
    std::atomic<uint64_t> boundary_{0};
    std::atomic<uint64_t> play_{0};
};

}  // namespace geode::sound
