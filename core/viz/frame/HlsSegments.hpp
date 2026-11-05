#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace geode::viz::frame::hls {

// The timing of Cast's live HLS stream (#656), beside the frame clock that stamps its PTS (#572).
//
// cast/LiveSegments (Kotlin) cuts the fragmented MP4 the encoder's muxer writes into an init segment
// (ftyp + moov) and one media segment per moof + mdat, keeps their bytes and serves them. What each
// segment lasts, the playlist's TARGETDURATION and the playlist text are decided here. A segment's
// duration is media time read from the fragment itself: the sum of the video track's sample durations in
// its trun, over the timescale that track's mdhd names. It used to be the wall time between two cuts,
// which follows how fast the encoder happened to drain, not what the TV will play: a slow drain then a
// quick one made a 1.4 s and a 0.6 s EXTINF for two one-second fragments, and TARGETDURATION jumped to 2.
//
// Only the boxes on the way are read (moov/trak/{tkhd,mdia/{mdhd,hdlr}}, moov/mvex/trex,
// moof/traf/{tfhd,trun}); a box that claims more bytes than its parent has ends the walk, so nothing is
// read past the buffer. Pure maths, no I/O, testable on the host (core/tests/hls_segments_test.cpp).

constexpr int kWindow = 6;          // segments kept and listed: about six seconds at one-second fragments
constexpr int kStartSegments = 3;   // HLS players start three segments back from the live edge
constexpr double kMinSegmentSec = 0.001;
constexpr int kMaxTracks = 8;

namespace detail {

inline uint32_t be32(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}
inline uint64_t be64(const uint8_t* p) { return (uint64_t{be32(p)} << 32) | be32(p + 4); }

// Calls f(type, body, bodySize) for each child box in [p, p + n); stops at the first malformed one.
template <typename F>
void forEachBox(const uint8_t* p, size_t n, F&& f) {
    size_t at = 0;
    while (n - at >= 8) {
        uint64_t size = be32(p + at);
        size_t header = 8;
        if (size == 1) {
            if (n - at < 16) return;
            size = be64(p + at + 8);
            header = 16;
        } else if (size == 0) {
            size = n - at;   // to the end of the parent
        }
        if (size < header || size > n - at) return;
        f(p + at + 4, p + at + header, static_cast<size_t>(size) - header);
        at += static_cast<size_t>(size);
    }
}

inline bool is(const uint8_t* type, const char* name) { return std::memcmp(type, name, 4) == 0; }

struct TrackTiming {
    uint32_t id = 0;
    uint32_t timescale = 0;
    uint32_t defaultDuration = 0;   // trex
    bool video = false;
};

struct Tracks {
    TrackTiming t[kMaxTracks];
    int count = 0;

    TrackTiming* find(uint32_t id) {
        for (int i = 0; i < count; ++i)
            if (t[i].id == id) return &t[i];
        return nullptr;
    }
};

inline void readTrak(const uint8_t* p, size_t n, Tracks& out) {
    TrackTiming track;
    bool haveId = false;
    forEachBox(p, n, [&](const uint8_t* type, const uint8_t* b, size_t len) {
        if (is(type, "tkhd") && len >= 4) {
            const size_t off = b[0] == 1 ? 4 + 16 : 4 + 8;
            if (len >= off + 4) {
                track.id = be32(b + off);
                haveId = true;
            }
        } else if (is(type, "mdia")) {
            forEachBox(b, len, [&](const uint8_t* t2, const uint8_t* b2, size_t len2) {
                if (is(t2, "mdhd") && len2 >= 4) {
                    const size_t off = b2[0] == 1 ? 4 + 16 : 4 + 8;
                    if (len2 >= off + 4) track.timescale = be32(b2 + off);
                } else if (is(t2, "hdlr") && len2 >= 12) {
                    track.video = is(b2 + 8, "vide");
                }
            });
        }
    });
    if (haveId && out.count < kMaxTracks) out.t[out.count++] = track;
}

// Every track's id, timescale, kind and trex default, from the init segment (ftyp + moov, or moov alone).
inline Tracks readTracks(const uint8_t* init, size_t n) {
    Tracks out;
    if (!init) return out;
    forEachBox(init, n, [&](const uint8_t* type, const uint8_t* b, size_t len) {
        if (!is(type, "moov")) return;
        forEachBox(b, len, [&](const uint8_t* t2, const uint8_t* b2, size_t len2) {
            if (is(t2, "trak")) readTrak(b2, len2, out);
        });
        // trex after every trak, whatever order moov lists them in.
        forEachBox(b, len, [&](const uint8_t* t2, const uint8_t* b2, size_t len2) {
            if (!is(t2, "mvex")) return;
            forEachBox(b2, len2, [&](const uint8_t* t3, const uint8_t* b3, size_t len3) {
                if (!is(t3, "trex") || len3 < 16) return;
                if (TrackTiming* t = out.find(be32(b3 + 4))) t->defaultDuration = be32(b3 + 12);
            });
        });
    });
    return out;
}

// One traf's track id and the sum of its sample durations, in that track's timescale units.
inline void readTraf(const uint8_t* p, size_t n, Tracks& tracks, uint32_t& trackId, uint64_t& units) {
    uint32_t tfhdDefault = 0;
    bool haveTfhdDefault = false;
    trackId = 0;
    units = 0;
    forEachBox(p, n, [&](const uint8_t* type, const uint8_t* b, size_t len) {
        if (!is(type, "tfhd") || len < 8) return;
        const uint32_t flags = be32(b) & 0xFFFFFF;
        trackId = be32(b + 4);
        size_t off = 8;
        if (flags & 0x01) off += 8;   // base data offset
        if (flags & 0x02) off += 4;   // sample description index
        if ((flags & 0x08) && len >= off + 4) {
            tfhdDefault = be32(b + off);
            haveTfhdDefault = true;
        }
    });
    const TrackTiming* track = tracks.find(trackId);
    const uint32_t fallback = haveTfhdDefault ? tfhdDefault : (track ? track->defaultDuration : 0);
    forEachBox(p, n, [&](const uint8_t* type, const uint8_t* b, size_t len) {
        if (!is(type, "trun") || len < 8) return;
        const uint32_t flags = be32(b) & 0xFFFFFF;
        const uint32_t count = be32(b + 4);
        size_t off = 8;
        if (flags & 0x001) off += 4;   // data offset
        if (flags & 0x004) off += 4;   // first sample flags
        size_t stride = 0;
        for (uint32_t bit : {0x100u, 0x200u, 0x400u, 0x800u})
            if (flags & bit) stride += 4;
        if (!(flags & 0x100)) {
            units += uint64_t{count} * fallback;
            return;
        }
        for (uint32_t i = 0; i < count && off + 4 <= len; ++i, off += stride) units += be32(b + off);
    });
}

}  // namespace detail

// How long a fragment (its moof box, alone or followed by its mdat) plays, in seconds of media time:
// its video track's samples over the video timescale; with no video track, the longest track. 0 when
// the init segment or the fragment says nothing usable.
inline double fragmentSeconds(const uint8_t* init, size_t initSize, const uint8_t* fragment, size_t fragmentSize) {
    detail::Tracks tracks = detail::readTracks(init, initSize);
    if (!fragment || tracks.count == 0) return 0.0;
    double video = -1.0;
    double longest = 0.0;
    detail::forEachBox(fragment, fragmentSize, [&](const uint8_t* type, const uint8_t* b, size_t len) {
        if (!detail::is(type, "moof")) return;
        detail::forEachBox(b, len, [&](const uint8_t* t2, const uint8_t* b2, size_t len2) {
            if (!detail::is(t2, "traf")) return;
            uint32_t id = 0;
            uint64_t units = 0;
            detail::readTraf(b2, len2, tracks, id, units);
            const detail::TrackTiming* track = tracks.find(id);
            if (!track || track->timescale == 0) return;
            const double sec = static_cast<double>(units) / track->timescale;
            if (track->video && sec > video) video = sec;
            if (sec > longest) longest = sec;
        });
    });
    return video >= 0.0 ? video : longest;
}

// EXT-X-TARGETDURATION: the longest listed segment rounded up to whole seconds, at least 1. A duration
// within a microsecond of a whole second counts as that second, so float noise on an exact one-second
// fragment never makes the target 2.
inline int targetDuration(const double* durations, int count) {
    double longest = 0.0;
    for (int i = 0; i < count; ++i)
        if (durations[i] > longest) longest = durations[i];
    const int target = static_cast<int>(std::ceil(longest - 1e-6));
    return target > 1 ? target : 1;
}

// The live media playlist for the listed segments (sequence first, first + 1, …), or "" until there are
// kStartSegments of them: a playlist shorter than where the receiver starts stalls it. The URIs are the
// server's (cast/LiveHttpServer routes them): the init segment's, and each segment's prefix + sequence +
// suffix.
inline std::string playlist(int64_t firstSequence, const double* durations, int count, const char* initUri,
                            const char* segmentPrefix, const char* segmentSuffix) {
    if (!durations || count < kStartSegments) return {};
    std::string out = "#EXTM3U\n#EXT-X-VERSION:7\n";
    char line[96];
    std::snprintf(line, sizeof line, "#EXT-X-TARGETDURATION:%d\n", targetDuration(durations, count));
    out += line;
    std::snprintf(line, sizeof line, "#EXT-X-MEDIA-SEQUENCE:%lld\n", static_cast<long long>(firstSequence));
    out += line;
    out += "#EXT-X-MAP:URI=\"";
    out += initUri ? initUri : "";
    out += "\"\n";
    for (int i = 0; i < count; ++i) {
        const double d = durations[i] > kMinSegmentSec ? durations[i] : kMinSegmentSec;
        std::snprintf(line, sizeof line, "#EXTINF:%.3f,\n", d);
        out += line;
        out += segmentPrefix ? segmentPrefix : "";
        out += std::to_string(firstSequence + i);
        out += segmentSuffix ? segmentSuffix : "";
        out += '\n';
    }
    return out;
}

}  // namespace geode::viz::frame::hls
