#include "audio/ingest/Cursor.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

#include "audio/ingest/Downmix.hpp"

namespace geode::audio::ingest {

namespace {

// A read is retried this often when the writer lapped it mid-copy; each retry starts further ahead, so
// only a writer that outruns the whole ring every iteration exhausts it (and then the read is empty).
constexpr int kAttempts = 4;

// How many format records one raw tail may span; past that it is cut short at the boundary.
constexpr int kTailSegments = 8;

bool plainStereo(const Record& r) {
    return r.channels == 2 && r.mask == (downmix::kFrontLeft | downmix::kFrontRight);
}

}  // namespace

Cursor::Cursor(const Ring& ring, Start start) : ring_(ring) {
    const int64_t written = ring.written();
    position_ = start == Start::Newest ? written : ring.oldestSafe(written);
}

int64_t Cursor::behind() const {
    return std::max<int64_t>(0, ring_.written() - position_);
}

int64_t Cursor::skipToNewest(int64_t keepFrames) {
    const int64_t target = std::max(position_, ring_.written() - std::max<int64_t>(keepFrames, 0));
    const int64_t skipped = target - position_;
    pendingDropped_ += skipped;
    position_ = target;
    return skipped;
}

bool Cursor::locate(int64_t position, int64_t written, int64_t records, Record& rec, int64_t& end) const {
    end = written;
    const int64_t floor = std::max<int64_t>(0, records - Ring::kRecords);
    for (int64_t i = records - 1; i >= floor; --i) {
        Record r;
        if (!ring_.record(i, r)) return false;
        if (r.start <= position) {
            rec = r;
            return true;
        }
        end = std::min(end, r.start);   // starts never decrease, so the last one passed is the next boundary
    }
    return false;
}

int64_t Cursor::oldestCovered(int64_t records) const {
    for (int64_t i = std::max<int64_t>(0, records - Ring::kRecords); i < records; ++i) {
        Record r;
        if (ring_.record(i, r)) return r.start;
    }
    return ring_.written();
}

void Cursor::loadGains(const Record& rec) {
    if (rec.channels == gainsChannels_ && rec.mask == gainsMask_) return;
    downmix::gains(rec.mask, rec.channels, left_, right_);
    gainsChannels_ = rec.channels;
    gainsMask_ = rec.mask;
}

Cursor::Block Cursor::read(float* stereoOut, int maxFrames) {
    Block block;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        const int64_t written = ring_.written();
        const int64_t records = ring_.records();
        const int64_t safe = ring_.oldestSafe(written);
        if (position_ < safe) {
            pendingDropped_ += safe - position_;
            position_ = safe;
        }
        if (position_ >= written || !stereoOut || maxFrames <= 0) break;

        Record rec;
        int64_t end = written;
        if (!locate(position_, written, records, rec, end)) {
            // Older than every format record still held: more format changes than kRecords went by
            // unread. Resume where the oldest held record starts.
            const int64_t oldest = std::min(oldestCovered(records), written);
            if (oldest > position_) {
                pendingDropped_ += oldest - position_;
                position_ = oldest;
            }
            continue;
        }
        end = std::min(end, position_ + maxFrames);
        const int n = static_cast<int>(end - position_);
        if (rec.channels <= 0) {
            // A record for an unusable format covers no samples (writes drop); step past it.
            position_ = end;
            continue;
        }

        if (plainStereo(rec)) {
            for (int i = 0; i < n; ++i) {
                const float* f = ring_.frame(position_ + i);
                stereoOut[2 * i] = f[0];
                stereoOut[2 * i + 1] = f[1];
            }
        } else {
            loadGains(rec);
            const int ch = rec.channels;
            for (int i = 0; i < n; ++i) {
                const float* f = ring_.frame(position_ + i);
                float l = 0.0f;
                float r = 0.0f;
                for (int c = 0; c < ch; ++c) {
                    l += left_[c] * f[c];
                    r += right_[c] * f[c];
                }
                stereoOut[2 * i] = l;
                stereoOut[2 * i + 1] = r;
            }
        }

        // The seqlock check: if the writer may have reached these slots while we copied, the copy may be
        // torn. Throw it away; the next attempt skips to what is safe and counts the drop.
        std::atomic_thread_fence(std::memory_order_acquire);
        if (position_ < ring_.oldestSafe(ring_.written())) continue;

        block.frames = n;
        block.position = position_;
        block.sampleRate = rec.sampleRate;
        block.sourceChannels = rec.sourceChannels;
        block.channelMask = rec.mask;
        block.epoch = rec.epoch;
        block.restarted = !hasEpoch_ || rec.epoch != epoch_;
        hasEpoch_ = true;
        epoch_ = rec.epoch;
        position_ = end;
        break;
    }
    block.dropped = pendingDropped_;
    pendingDropped_ = 0;
    return block;
}

int Cursor::rawMonoTail(float* out, int n) const {
    if (!out || n <= 0 || !hasEpoch_) return 0;
    const int64_t end = position_;
    const int64_t written = ring_.written();
    const int64_t records = ring_.records();
    int64_t lo = std::max(end - n, ring_.oldestSafe(written));
    if (lo >= end) return 0;

    // Walk the records from the newest back, collecting the stretches of [lo, end) each covers, until the
    // walk reaches lo, leaves this epoch, or runs out of held records.
    struct Segment {
        int64_t from;
        int64_t to;
        Record rec;
    };
    Segment segments[kTailSegments];
    int count = 0;
    int64_t to = written;     // where record i stops covering
    int64_t reached = end;    // the walk covers [reached, end)
    const int64_t floor = std::max<int64_t>(0, records - Ring::kRecords);
    for (int64_t i = records - 1; i >= floor && reached > lo; --i) {
        Record r;
        if (!ring_.record(i, r)) break;
        const int64_t recordTo = to;
        to = std::min(to, r.start);
        if (r.start >= end) continue;
        if (r.epoch != epoch_ || count == kTailSegments) break;
        const int64_t a = std::max(r.start, lo);
        const int64_t b = std::min(recordTo, end);
        if (a < b && r.channels > 0) segments[count++] = {a, b, r};
        reached = r.start;
    }
    lo = std::max(lo, reached);
    if (lo >= end) return 0;

    for (int s = 0; s < count; ++s) {
        const Segment& seg = segments[s];
        const int64_t a = std::max(seg.from, lo);
        float left[Ring::kMaxChannels];
        float right[Ring::kMaxChannels];
        downmix::gains(seg.rec.mask, seg.rec.channels, left, right);
        for (int64_t p = a; p < seg.to; ++p) {
            const float* f = ring_.frame(p);
            float sum = 0.0f;
            for (int c = 0; c < seg.rec.channels; ++c) sum += (left[c] + right[c]) * f[c];
            out[p - lo] = 0.5f * sum;
        }
    }

    // Same check as read(): keep only the part the writer cannot have reached while we copied.
    std::atomic_thread_fence(std::memory_order_acquire);
    const int64_t safe = ring_.oldestSafe(ring_.written());
    if (safe > lo) {
        if (safe >= end) return 0;
        std::memmove(out, out + (safe - lo), static_cast<size_t>(end - safe) * sizeof(float));
        lo = safe;
    }
    return static_cast<int>(end - lo);
}

}  // namespace geode::audio::ingest
