#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "viz/frame/HlsSegments.hpp"

namespace geode::cast {

// The live HLS stream a TV plays and the fragments a browser is fed, cut from the one continuous fragmented
// MP4 the cast encoder's muxer writes: ftyp and moov once, then a moof and mdat pair per fragment. HLS with
// fMP4 segments (CMAF) wants exactly that split up: the header as the init segment (#EXT-X-MAP) and each
// moof + mdat as one media segment. write() takes the muxer's bytes in whatever pieces they come and cuts
// them at box boundaries. The muxer's fragments only make sense inside its one file (absolute offsets, no
// decode time), so each is rewritten into a self-contained segment before it is kept
// (viz/frame/HlsSegments: rewriteFragment), and its #EXTINF is the media time it holds.
//
// Only the newest hls::kWindow segments are kept, in memory: a live playlist only ever names the last few,
// and a receiver starts near the live edge.
//
// The input is the app's own muxer, but nothing here trusts it: a box that claims fewer bytes than its own
// header, or more than kMaxBoxBytes, ends the stream (broken()), so what is held is bounded and no size is
// ever followed past the bytes that are here. The freeze watchdog then ends the cast.
//
// Written from the encoder's drain thread, read from the server's: one mutex. Bytes are handed out as shared
// immutable buffers, so a slow client is written to without the lock and without a copy.
// Pure, no I/O; tested by core/tests/live_segments_test.cpp.
using Bytes = std::shared_ptr<const std::vector<uint8_t>>;

class LiveSegments {
public:
    // A fragment is about half a megabyte at the cast's bit rate; nothing the muxer writes comes near this.
    static constexpr uint64_t kMaxBoxBytes = 32u * 1024u * 1024u;

    // nowMs: a monotonic clock in ms, read only for msSinceLastSegment (the freeze watchdog), never for timing.
    explicit LiveSegments(std::function<int64_t()> nowMs);

    // The next bytes of the muxer's output.
    void write(const uint8_t* data, size_t size);

    // The init segment (ftyp + moov); null until the muxer has written its header.
    Bytes init() const;
    // A browser's master playlist (CODECS from the init segment); "" until the header is here.
    std::string masterPlaylist() const;
    // The segment with this sequence number; null once it left the window, or before it exists.
    Bytes segment(int64_t sequence) const;

    struct Next {
        int64_t sequence = -1;
        Bytes bytes;
    };
    // The browser feed's next fragment after sequence `after`; false while there is none yet. A new feed
    // (after < 0) starts at the newest, the live edge; a feed that fell out of the window skips to the
    // newest too, since a browser holding half a second would rather jump than play old seconds.
    bool nextAfter(int64_t after, Next& out) const;

    // The live media playlist; "" until there is enough for a receiver to start on (hls::kStartSegments).
    std::string playlist() const;

    // Milliseconds since the last segment was cut (or the header was written); 0 before that.
    int64_t msSinceLastSegment() const;
    // Fragments kept as the muxer wrote them because they could not be rewritten.
    int64_t servedAsMuxed() const;
    // True once a box no stream could hold ended the cutting.
    bool broken() const;

private:
    struct Segment {
        int64_t sequence;
        Bytes bytes;
        double seconds;
    };

    void onBox(const uint8_t* box, size_t size);   // lock_
    void publish();                                // lock_

    const std::function<int64_t()> nowMs_;
    mutable std::mutex lock_;
    std::vector<uint8_t> pending_;    // bytes not yet a whole box
    std::vector<uint8_t> header_;     // ftyp + moov as they arrive
    std::vector<uint8_t> fragment_;   // the open fragment: its moof, until its mdat arrives
    bool inFragment_ = false;
    std::deque<Segment> segments_;
    Bytes init_;
    mutable std::string master_;      // made once per init segment
    int64_t nextSequence_ = 0;
    int64_t fileAt_ = 0;              // where the next box starts in the muxer's output
    int64_t moofAt_ = 0;              // where the open fragment's moof did
    int64_t decodeTimes_[viz::frame::hls::kMaxTracks] = {};
    int64_t lastCutMs_ = 0;
    bool cut_ = false;
    int64_t servedAsMuxed_ = 0;
    bool broken_ = false;
};

}  // namespace geode::cast
