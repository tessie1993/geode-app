#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The stage's per-frame ring for measuring a phone the owner does not have: what each paced frame cost the stage
// thread, what its GPU work took, and how far its presentation was from the last one's. Off by default; only a debug
// flag turns it on (core/stage/android/StageThread), so a normal run neither allocates nor records. Pure, so the host
// can test the ring and its CSV (core/tests/frame_recorder_test.cpp).
namespace geode::stage {

struct FrameRecord {
    // The stage thread's work for the frame: the client's draw, not the swap's wait.
    int64_t cpuDrawNs = 0;
    // The frame's GPU work as the pacer measured it (Swappy's postWait: its fence's time), told one frame late.
    // 0: not known (no pacer measure, or the next frame never came).
    int64_t gpuNs = 0;
    // The frame's presentation time after the last drawn frame's; 0 for the first frame after a start.
    int64_t presentIntervalNs = 0;
};

class FrameRecorder {
public:
    // About 4 s at 120 Hz: a chunk the stage hands over is a few hundred log lines.
    static constexpr size_t kCapacity = 512;
    static constexpr const char* kCsvHeader = "frame,cpuDrawNs,gpuNs,presentIntervalNs\n";

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

    // A frame drawn and swapped: how long the stage thread took to draw it.
    void drawn(int64_t cpuDrawNs, int64_t presentIntervalNs) {
        if (!enabled_) return;
        FrameRecord record;
        record.cpuDrawNs = cpuDrawNs;
        record.presentIntervalNs = presentIntervalNs;
        push(record);
    }
    // The frame before the newest one took `gpuNs` on the GPU (a pacer reports it at the next frame's swap). Not a
    // time (0 or less), or a frame the ring no longer holds: nothing.
    void gpuOfPrevious(int64_t gpuNs) {
        if (!enabled_ || gpuNs <= 0 || size_ < 2) return;
        slot(size_ - 2).gpuNs = gpuNs;
    }

    size_t size() const { return size_; }
    bool full() const { return size_ == kCapacity; }
    // The number of the oldest record held; numbers go on across clear(), so handed-over chunks join.
    uint64_t firstFrame() const { return next_ - size_; }
    // Oldest first.
    const FrameRecord& at(size_t i) const { return ring_[(head_ + i) % kCapacity]; }

    // Hands the ring over: forgets its records, keeps the numbering.
    void clear() {
        head_ = 0;
        size_ = 0;
    }

    std::string csv() const {
        std::string out = kCsvHeader;
        for (size_t i = 0; i < size_; ++i) {
            const FrameRecord& r = at(i);
            out += std::to_string(firstFrame() + i);
            out += ',';
            out += std::to_string(r.cpuDrawNs);
            out += ',';
            out += std::to_string(r.gpuNs);
            out += ',';
            out += std::to_string(r.presentIntervalNs);
            out += '\n';
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
};

}  // namespace geode::stage
