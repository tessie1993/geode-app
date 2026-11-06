#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "viz/frame/FixedRateClock.hpp"

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
// TARGETDURATION is now fixed for the stream from the nominal segment length (the cast keyframe
// interval), since RFC 8216 6.2.1 forbids changing it, and a fragment that cannot be read is listed at
// that nominal length. Media3 1.11's FragmentedMp4Muxer writes tfhd flags 0x000001 (a base data offset),
// trun version 1 with flags 0x701 and no tfdt; tfdt and composition offsets (0xF01) parse too.
//
// Only the boxes on the way are read (moov/trak/{tkhd,mdia/{mdhd,hdlr}}, moov/mvex/trex,
// moof/traf/{tfhd,trun}); a box that claims more bytes than its parent has ends the walk, so nothing is
// read past the buffer. Pure maths, no I/O, testable on the host (core/tests/hls_segments_test.cpp).

constexpr int kWindow = 6;          // segments kept and listed: about six seconds at one-second fragments
constexpr int kStartSegments = 3;   // HLS players start three segments back from the live edge
constexpr double kNominalSegmentSec = CastFormat::kKeyframeIntervalSec;   // one fragment per keyframe
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

inline bool isType(const uint8_t* type, const char* name) { return std::memcmp(type, name, 4) == 0; }

// tkhd's track_ID and mdhd's timescale both follow the version-dependent creation and modification
// times (two 32-bit fields in version 0, two 64-bit ones in version 1) after the full-box header.
inline bool readFieldAfterTimes(const uint8_t* body, size_t len, uint32_t& out) {
    if (len < 4) return false;
    const size_t off = body[0] == 1 ? 4 + 16 : 4 + 8;
    if (len < off + 4) return false;
    out = be32(body + off);
    return true;
}

struct TrackTiming {
    uint32_t id = 0;
    uint32_t timescale = 0;
    uint32_t defaultDuration = 0;   // trex
    bool video = false;
};

struct Tracks {
    TrackTiming list[kMaxTracks];
    int count = 0;

    TrackTiming* find(uint32_t id) {
        for (int i = 0; i < count; ++i)
            if (list[i].id == id) return &list[i];
        return nullptr;
    }
};

inline void readTrak(const uint8_t* trak, size_t trakLen, Tracks& out) {
    TrackTiming track;
    bool haveId = false;
    forEachBox(trak, trakLen, [&](const uint8_t* type, const uint8_t* body, size_t len) {
        if (isType(type, "tkhd")) {
            haveId = readFieldAfterTimes(body, len, track.id);
        } else if (isType(type, "mdia")) {
            forEachBox(body, len, [&](const uint8_t* mdiaType, const uint8_t* mdiaBody, size_t mdiaLen) {
                if (isType(mdiaType, "mdhd")) {
                    readFieldAfterTimes(mdiaBody, mdiaLen, track.timescale);
                } else if (isType(mdiaType, "hdlr") && mdiaLen >= 12) {
                    track.video = isType(mdiaBody + 8, "vide");
                }
            });
        }
    });
    if (haveId && out.count < kMaxTracks) out.list[out.count++] = track;
}

// Every track's id, timescale, kind and trex default, from the init segment (ftyp + moov, or moov alone).
inline Tracks readTracks(const uint8_t* init, size_t n) {
    Tracks out;
    if (!init) return out;
    forEachBox(init, n, [&](const uint8_t* type, const uint8_t* moov, size_t moovLen) {
        if (!isType(type, "moov")) return;
        forEachBox(moov, moovLen, [&](const uint8_t* childType, const uint8_t* trak, size_t trakLen) {
            if (isType(childType, "trak")) readTrak(trak, trakLen, out);
        });
        // trex after every trak, whatever order moov lists them in.
        forEachBox(moov, moovLen, [&](const uint8_t* childType, const uint8_t* mvex, size_t mvexLen) {
            if (!isType(childType, "mvex")) return;
            forEachBox(mvex, mvexLen, [&](const uint8_t* mvexType, const uint8_t* trex, size_t trexLen) {
                if (!isType(mvexType, "trex") || trexLen < 16) return;
                if (TrackTiming* track = out.find(be32(trex + 4))) track->defaultDuration = be32(trex + 12);
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
        if (!isType(type, "tfhd") || len < 8) return;
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
        if (!isType(type, "trun") || len < 8) return;
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
// the init segment or the fragment says nothing usable (the playlist then lists the nominal length).
inline double fragmentSeconds(const uint8_t* init, size_t initSize, const uint8_t* fragment, size_t fragmentSize) {
    detail::Tracks tracks = detail::readTracks(init, initSize);
    if (!fragment || tracks.count == 0) return 0.0;
    double video = -1.0;
    double longest = 0.0;
    detail::forEachBox(fragment, fragmentSize, [&](const uint8_t* type, const uint8_t* b, size_t len) {
        if (!detail::isType(type, "moof")) return;
        detail::forEachBox(b, len, [&](const uint8_t* moofType, const uint8_t* traf, size_t trafLen) {
            if (!detail::isType(moofType, "traf")) return;
            uint32_t id = 0;
            uint64_t units = 0;
            detail::readTraf(traf, trafLen, tracks, id, units);
            const detail::TrackTiming* track = tracks.find(id);
            if (!track || track->timescale == 0) return;
            const double sec = static_cast<double>(units) / track->timescale;
            if (track->video && sec > video) video = sec;
            if (sec > longest) longest = sec;
        });
    });
    return video >= 0.0 ? video : longest;
}

// EXT-X-TARGETDURATION for a stream whose segments nominally last nominalSec: fixed for the whole
// stream (RFC 8216 6.2.1: it MUST NOT change), never recomputed over the window, where one late keyframe
// (a 31-frame, 1.033 s fragment) used to move it 1 -> 2 -> 1 between reloads. Rounded to the nearest
// second like the EXTINF rule it bounds (4.3.3.1: each EXTINF rounded to the nearest integer is at most
// it), at least 1, so fragments up to half a second over the keyframe interval stay within it.
inline int targetDuration(double nominalSec) {
    const int target = static_cast<int>(std::lround(nominalSec));
    return target > 1 ? target : 1;
}

// The live media playlist for the listed segments (sequence first, first + 1, …), or "" until there are
// kStartSegments of them: a playlist shorter than where the receiver starts stalls it. Only the newest
// kWindow are listed. A duration of 0 (a fragment that could not be read) is listed as the nominal
// segment, which is what the fragment really holds near enough, not as a 1 ms sliver. The URIs are the
// server's (cast/LiveHttpServer routes them): the init segment's, and each segment's prefix + sequence +
// suffix.
inline std::string playlist(int64_t firstSequence, const double* durations, int count, const char* initUri,
                            const char* segmentPrefix, const char* segmentSuffix) {
    if (!durations || count < kStartSegments) return {};
    if (count > kWindow) {
        durations += count - kWindow;
        firstSequence += count - kWindow;
        count = kWindow;
    }
    std::string out = "#EXTM3U\n#EXT-X-VERSION:7\n";
    char line[96];
    std::snprintf(line, sizeof line, "#EXT-X-TARGETDURATION:%d\n", targetDuration(kNominalSegmentSec));
    out += line;
    std::snprintf(line, sizeof line, "#EXT-X-MEDIA-SEQUENCE:%lld\n", static_cast<long long>(firstSequence));
    out += line;
    out += "#EXT-X-MAP:URI=\"";
    out += initUri ? initUri : "";
    out += "\"\n";
    for (int i = 0; i < count; ++i) {
        const double d = durations[i] > 0.0 ? durations[i] : kNominalSegmentSec;
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
