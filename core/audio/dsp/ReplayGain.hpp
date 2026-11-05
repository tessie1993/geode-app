#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace geode::audio {

// ReplayGain for the playback chain: the gain rule, and a small table of the tags of the tracks around
// the one playing, keyed by stream.
//
// Why the table is here and keyed by stream (#351): the chain processes audio ahead of the speaker by
// one sink buffer or more, so a gain set when the player *announces* a new track (on the main thread,
// at playout time) lands on the wrong samples: the head of track N+1 plays at track N's gain. The only
// place that knows which track a buffer belongs to is the processing pipeline itself, which Media3
// flushes with the new stream's identity when that stream starts, in stream order. So the main thread
// reads the tags of the next tracks ahead of time and hands them over here; the chain picks its gain
// at the stream start (DspChain::beginStream), and never drops to 0 dB while a read is still out.

// The bits of geode_tags_replaygain's mask, in its value order.
enum ReplayGainBits : int { kTrackGain = 1, kTrackPeak = 2, kAlbumGain = 4, kAlbumPeak = 8 };

enum ReplayGainMode : int { kReplayGainOff = 0, kReplayGainTrack = 1, kReplayGainAlbum = 2 };

// A track's tags as the file carries them: `mask` says which of the four values exist.
struct ReplayGainTags {
    int mask = 0;
    float trackGainDb = 0.0f;
    float trackPeak = 0.0f;
    float albumGainDb = 0.0f;
    float albumPeak = 0.0f;
};

// The tag gain plus preamp, held below the level at which the tagged peak would clip. The other mode's
// value stands in when the chosen one is missing; a track with neither plays at 0 dB.
inline float replayGainDb(const ReplayGainTags& t, int mode, float preampDb, bool clipGuard) {
    if (mode != kReplayGainTrack && mode != kReplayGainAlbum) return 0.0f;
    const bool album = mode == kReplayGainAlbum;
    const bool hasTrack = (t.mask & kTrackGain) != 0;
    const bool hasAlbum = (t.mask & kAlbumGain) != 0;
    if (!hasTrack && !hasAlbum) return 0.0f;
    const float gain = album ? (hasAlbum ? t.albumGainDb : t.trackGainDb) : (hasTrack ? t.trackGainDb : t.albumGainDb);
    const bool hasTrackPeak = (t.mask & kTrackPeak) != 0;
    const bool hasAlbumPeak = (t.mask & kAlbumPeak) != 0;
    bool hasPeak = false;
    float peak = 0.0f;
    if (album ? hasAlbumPeak : hasTrackPeak) {
        hasPeak = true;
        peak = album ? t.albumPeak : t.trackPeak;
    } else if (album ? hasTrackPeak : hasAlbumPeak) {
        hasPeak = true;
        peak = album ? t.trackPeak : t.albumPeak;
    }
    const float wanted = gain + preampDb;
    if (!clipGuard || !hasPeak || !(peak > 0.0f)) return wanted;
    return std::min(wanted, -20.0f * std::log10(peak));
}

// A stream's key: FNV-1a over its name (the item's URI); 0 means "no stream known".
inline uint64_t replayGainStreamKey(const char* name) {
    if (name == nullptr || *name == '\0') return 0;
    uint64_t h = 1469598103934665603ULL;
    for (const char* p = name; *p != '\0'; ++p) {
        h ^= static_cast<unsigned char>(*p);
        h *= 1099511628211ULL;
    }
    return h == 0 ? 1 : h;
}

// The settings and the tags of the last few streams. Written from any thread (the main thread); read by
// the audio thread through tryGain, which never blocks: on a busy lock it reports Busy and the chain
// asks again on its next block.
class ReplayGainTable {
public:
    static constexpr size_t kCapacity = 32;

    enum class Lookup { Found, Unknown, Busy };

    void configure(int mode, float preampDb, bool clipGuard) {
        std::lock_guard<std::mutex> lock(mutex_);
        mode_ = mode;
        preampDb_ = preampDb;
        clipGuard_ = clipGuard;
        bump();
    }

    // A stream's tags; the same key again replaces them. Past kCapacity the oldest entry goes.
    void setTags(uint64_t key, const ReplayGainTags& tags) {
        if (key == 0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        Entry* slot = nullptr;
        for (auto& e : entries_) {
            if (e.key == key) slot = &e;
        }
        if (slot == nullptr) {
            slot = &entries_[next_];
            next_ = (next_ + 1) % kCapacity;
        }
        slot->key = key;
        slot->tags = tags;
        bump();
    }

    // Changes whenever the settings or any tags change; the chain re-reads its gain when it moves.
    uint64_t generation() const { return generation_.load(std::memory_order_acquire); }

    // The gain for `key`, and the generation it was read at. Unknown: the key's tags have not arrived
    // (the caller keeps the gain it has). With ReplayGain off every key is Found at 0 dB.
    Lookup tryGain(uint64_t key, float& db, uint64_t& generation) const {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return Lookup::Busy;
        generation = generation_.load(std::memory_order_relaxed);
        if (mode_ != kReplayGainTrack && mode_ != kReplayGainAlbum) {
            db = 0.0f;
            return Lookup::Found;
        }
        if (key == 0) return Lookup::Unknown;
        for (const auto& e : entries_) {
            if (e.key == key) {
                db = replayGainDb(e.tags, mode_, preampDb_, clipGuard_);
                return Lookup::Found;
            }
        }
        return Lookup::Unknown;
    }

private:
    struct Entry {
        uint64_t key = 0;
        ReplayGainTags tags;
    };

    void bump() { generation_.fetch_add(1, std::memory_order_acq_rel); }

    mutable std::mutex mutex_;
    std::array<Entry, kCapacity> entries_{};
    size_t next_ = 0;
    int mode_ = kReplayGainOff;
    float preampDb_ = 0.0f;
    bool clipGuard_ = true;
    std::atomic<uint64_t> generation_{1};
};

}  // namespace geode::audio
