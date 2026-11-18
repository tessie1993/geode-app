#include "audio/ingest/CapturePump.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "sound/Silence.hpp"
#include "util/Log.hpp"

namespace geode::audio::ingest {

// Shared by the pump and its thread, so a thread stop leaves behind (a read the platform never lets go of)
// still has its reader and buffer when the read returns.
struct CapturePump::Run {
    std::unique_ptr<CaptureReader> reader;
    Write write;
    Clock nowMs = nullptr;
    std::vector<unsigned char> buffer;
    std::atomic<bool> running{true};
    std::atomic<int64_t> heardAtMs{0};
    // Held across the running check and the write, so once stop has taken it no write follows.
    std::mutex writeLock;
    std::mutex doneLock;
    std::condition_variable doneSignal;
    bool done = false;
    // doneLock: stop left the thread behind, so it counts in gStuck until it ends.
    bool detached = false;
};

namespace {

constexpr const char* kTag = "GeodeCapture";

// CapturePump::stuck: threads stop left behind that have not ended yet.
std::atomic<int> gStuck{0};

void pump(const std::shared_ptr<CapturePump::Run>& run) {
    CaptureReader& reader = *run->reader;
    const int capacity = static_cast<int>(run->buffer.size());
    if (reader.begin()) {
        while (run->running.load(std::memory_order_acquire)) {
            const int n = reader.read(run->buffer.data(), capacity);
            if (n < 0) break;
            if (n == 0) continue;
            std::lock_guard<std::mutex> lock(run->writeLock);
            if (!run->running.load(std::memory_order_acquire)) break;
            const float peak = run->write(run->buffer.data(), static_cast<size_t>(std::min(n, capacity)));
            const int64_t heard = run->heardAtMs.load(std::memory_order_relaxed);
            run->heardAtMs.store(sound::silence::captureHeardAt(peak, run->nowMs(), heard), std::memory_order_relaxed);
        }
    }
    run->running.store(false, std::memory_order_release);
    reader.end();
    {
        std::lock_guard<std::mutex> lock(run->doneLock);
        run->done = true;
        if (run->detached) {
            const int left = gStuck.fetch_sub(1, std::memory_order_acq_rel) - 1;
            GEODE_LOGI(kTag, "a capture thread left behind has ended; %d still stuck", left);
        }
    }
    run->doneSignal.notify_all();
}

}  // namespace

std::unique_ptr<CapturePump> CapturePump::start(std::unique_ptr<CaptureReader> reader, Write write, int readBytes,
                                                Clock nowMs) {
    if (!reader) return nullptr;
    if (!write || readBytes <= 0 || !nowMs) {
        reader->end();
        return nullptr;
    }
    if (const int waiting = gStuck.load(std::memory_order_acquire); waiting >= kMaxStuck) {
        GEODE_LOGW(kTag, "capture refused: %d earlier capture threads are still stuck in a platform read", waiting);
        reader->end();
        return nullptr;
    }
    auto run = std::make_shared<Run>();
    run->reader = std::move(reader);
    run->write = std::move(write);
    run->nowMs = nowMs;
    run->buffer.resize(static_cast<size_t>(readBytes));
    run->heardAtMs.store(nowMs(), std::memory_order_relaxed);
    std::unique_ptr<CapturePump> made(new CapturePump(run));
    try {
        made->thread_ = std::thread(pump, run);
    } catch (const std::system_error&) {
        run->running.store(false, std::memory_order_release);
        run->reader->end();
        return nullptr;
    }
    return made;
}

void CapturePump::stop() {
    if (!thread_.joinable()) return;
    run_->running.store(false, std::memory_order_release);
    { std::lock_guard<std::mutex> lock(run_->writeLock); }
    run_->reader->interrupt();
    bool done = false;
    int leftBehind = 0;
    {
        std::unique_lock<std::mutex> lock(run_->doneLock);
        done = run_->doneSignal.wait_for(lock, std::chrono::milliseconds(kStopWaitMs), [this] { return run_->done; });
        // Under doneLock, so the thread's end sees the mark and takes itself off the count.
        if (!done) {
            run_->detached = true;
            leftBehind = gStuck.fetch_add(1, std::memory_order_acq_rel) + 1;
        }
    }
    if (done) {
        thread_.join();
    } else {
        thread_.detach();
        GEODE_LOGW(kTag, "capture read did not let go within %lld ms; thread left behind (%d stuck)",
                   static_cast<long long>(kStopWaitMs), leftBehind);
    }
}

int CapturePump::stuck() { return gStuck.load(std::memory_order_acquire); }

bool CapturePump::active() const { return run_->running.load(std::memory_order_acquire); }

bool CapturePump::quiet() const {
    return active() && sound::silence::captureQuiet(run_->nowMs(), run_->heardAtMs.load(std::memory_order_relaxed));
}

}  // namespace geode::audio::ingest
