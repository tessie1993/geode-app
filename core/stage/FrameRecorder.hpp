#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The stage's per-frame ring for measuring a phone the owner does not have: what each vsync the frame clock drew on
// cost the stage thread, when its fence was seen signalled, and how far its presentation was from the last one's.
// Off by default; only a debug flag turns it on (core/stage/android/StageThread), so a normal run neither allocates
// nor records. Pure, so the host can test the ring and its CSV (core/tests/frame_recorder_test.cpp).
namespace geode::stage {

struct FrameRecord {
    // The stage thread's work for the frame: the client's draw and the swap.
    int64_t cpuDrawNs = 0;
    // From the frame's CPU start to the first poll that found its fence signalled: an upper bound on when the GPU
    // finished, as fine as the vsyncs the stage polls on. 0: not seen (a skip, or the ring was handed over first).
    int64_t gpuDoneNs = 0;
    // The frame's expected presentation after the last drawn frame's; 0 for the first frame after a start.
    int64_t presentIntervalNs = 0;
    // A vsync the frame clock wanted that drew nothing: the GPU had not finished the last frame.
    bool skipped = false;
};

class FrameRecorder {
public:
    // About 4 s at 120 Hz: a chunk the stage hands over is a few hundred log lines.
    static constexpr size_t kCapacity = 512;
    static constexpr const char* kCsvHeader = "frame,cpuDrawNs,gpuDoneNs,presentIntervalNs,skipped\n";

    void setEnabled(bool on) {
        enabled_ = on;
        clear();
        if (on) {
            ring_.resize(kCapacity);
        } else {
            ring_.clear();
            ring_.shrink_to_fit();
        }
    }
    bool enabled() const { return enabled_; }

    // A frame drawn and swapped: its CPU start and how long the stage thread took.
    void drawn(int64_t startNs, int64_t cpuDrawNs, int64_t presentIntervalNs) {
        if (!enabled_) return;
        FrameRecord record;
        record.cpuDrawNs = cpuDrawNs;
        record.presentIntervalNs = presentIntervalNs;
        push(record);
        pendingFrame_ = next_ - 1;
        pendingStartNs_ = startNs;
        awaiting_ = true;
    }
    void skipped() {
        if (!enabled_) return;
        FrameRecord record;
        record.skipped = true;
        push(record);
    }
    // A poll found the last swapped frame's fence signalled at `nowNs`.
    void fenceSignalled(int64_t nowNs) {
        if (!awaiting_) return;
        awaiting_ = false;
        if (pendingFrame_ < firstFrame()) return;
        const int64_t done = nowNs - pendingStartNs_;
        slot(static_cast<size_t>(pendingFrame_ - firstFrame())).gpuDoneNs = done > 0 ? done : 1;
    }
    bool awaitingFence() const { return awaiting_; }

    size_t size() const { return size_; }
    bool full() const { return size_ == kCapacity; }
    // The number of the oldest record held; numbers go on across clear(), so handed-over chunks join.
    uint64_t firstFrame() const { return next_ - size_; }
    // Oldest first.
    const FrameRecord& at(size_t i) const { return ring_[(head_ + i) % kCapacity]; }

    // Hands the ring over: forgets its records and the fence it waited for, keeps the numbering.
    void clear() {
        head_ = 0;
        size_ = 0;
        awaiting_ = false;
    }

    std::string csv() const {
        std::string out = kCsvHeader;
        for (size_t i = 0; i < size_; ++i) {
            const FrameRecord& r = at(i);
            out += std::to_string(firstFrame() + i);
            out += ',';
            out += std::to_string(r.cpuDrawNs);
            out += ',';
            out += std::to_string(r.gpuDoneNs);
            out += ',';
            out += std::to_string(r.presentIntervalNs);
            out += r.skipped ? ",1\n" : ",0\n";
        }
        return out;
    }

private:
    FrameRecord& slot(size_t i) { return ring_[(head_ + i) % kCapacity]; }
    void push(const FrameRecord& record) {
        if (size_ < kCapacity) {
            slot(size_++) = record;
        } else {
            ring_[head_] = record;
            head_ = (head_ + 1) % kCapacity;
        }
        ++next_;
    }

    bool enabled_ = false;
    std::vector<FrameRecord> ring_;
    size_t head_ = 0;
    size_t size_ = 0;
    uint64_t next_ = 0;  // the number the next record gets
    bool awaiting_ = false;
    uint64_t pendingFrame_ = 0;
    int64_t pendingStartNs_ = 0;
};

}  // namespace geode::stage
