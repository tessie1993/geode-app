#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "viz/frame/FixedRateClock.hpp"

namespace geode::viz::frame::hls {

// The timing of Cast's live HLS stream (#656), beside the frame clock that stamps its PTS (#572).
//
// core/cast/LiveSegments cuts the fragmented MP4 the encoder's muxer writes into an init segment
// (ftyp + moov) and one media segment per moof + mdat, keeps their bytes and serves them. What each
// segment lasts, the playlist's TARGETDURATION and the playlist text are decided here. A segment's
// duration is media time read from the fragment itself: the sum of the video track's sample durations in
// its trun, over the timescale that track's mdhd names. It used to be the wall time between two cuts,
// which follows how fast the encoder happened to drain, not what the TV will play: a slow drain then a
// quick one made a 1.4 s and a 0.6 s EXTINF for two one-second fragments, and TARGETDURATION jumped to 2.
// TARGETDURATION is fixed for the stream (RFC 8216 6.2.1 forbids changing it), and a fragment that
// cannot be read is listed at the nominal length.
//
// Media3 1.11's FragmentedMp4Muxer writes each traf as tfhd flags 0x000001 (an absolute base data
// offset: the moof's position in the one continuous file) and trun version 1 flags 0x701, with no tfdt.
// Served as is, a segment means nothing on its own: a receiver joining at the live edge resolves the
// samples outside the bytes it has and has no decode time to place them (RFC 8216 3.3, CMAF). So
// rewriteFragment turns each fragment into a self-contained one before it is served: default-base-is-moof
// (0x020000) in place of the base offset, a version 1 tfdt with the track's running decode time, and the
// trun data offsets and box sizes moved by what that changed. tfdt and composition offsets (0xF01) parse
// too.
//
// Only the boxes on the way are read (moov/trak/{tkhd,mdia/{mdhd,hdlr}}, moov/mvex/trex,
// moof/traf/{tfhd,tfdt,trun}); a box that claims more bytes than its parent has ends the walk (or refuses
// the rewrite), so nothing is read past the buffer. Pure maths, no I/O, testable on the host
// (core/tests/hls_segments_test.cpp).

constexpr int kWindow = 6;          // segments kept and listed: about six seconds at one-second fragments
constexpr int kStartSegments = 3;   // HLS players start three segments back from the live edge
constexpr double kNominalSegmentSec = CastFormat::kKeyframeIntervalSec;   // one fragment per keyframe
constexpr int kMaxTracks = 8;
constexpr size_t kMaxGrowth = 20 * kMaxTracks;   // a tfdt per traf is all a rewrite adds
// A fragment runs from one keyframe at least kNominalSegmentSec after its first to the next: one
// interval and a frame or two, or up to two when the encoder misses one keyframe or the frame clock
// drops slots after a stall. The target covers that (each EXTINF rounds to at most it up to 2.5 s);
// past it the freeze watchdog (CastStream.FROZEN_MS) is what answers.
constexpr double kLongestSegmentSec = 2 * kNominalSegmentSec;

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
    uint32_t defaultSize = 0;       // trex
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
                TrackTiming* track = out.find(be32(trex + 4));
                if (!track) return;
                track->defaultDuration = be32(trex + 12);
                if (trexLen >= 20) track->defaultSize = be32(trex + 16);
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

// EXT-X-TARGETDURATION for a stream whose segments last at most longestSec: fixed for the whole stream
// (RFC 8216 6.2.1: it MUST NOT change), never recomputed over the window. Rounded to the nearest second
// like the EXTINF rule it bounds (4.3.3.1: each EXTINF rounded to the nearest integer is at most it), at
// least 1.
inline int targetDuration(double nominalSec) {
    const int target = static_cast<int>(std::lround(nominalSec));
    return target > 1 ? target : 1;
}

// The live media playlist for the listed segments (sequence first, first + 1, …), or "" until there are
// kStartSegments of them: a playlist shorter than where the receiver starts stalls it. Only the newest
// kWindow are listed. A duration of 0 (a fragment that could not be read) is listed as the nominal
// segment, which is what the fragment really holds near enough, not as a 1 ms sliver. The URIs are the
// server's (core/cast/LiveHttp routes them): the init segment's, and each segment's prefix + sequence +
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
    std::snprintf(line, sizeof line, "#EXT-X-TARGETDURATION:%d\n", targetDuration(kLongestSegmentSec));
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

namespace detail {

inline void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}
inline void put64(std::vector<uint8_t>& b, uint64_t v) {
    put32(b, static_cast<uint32_t>(v >> 32));
    put32(b, static_cast<uint32_t>(v));
}
inline void patch32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}
inline void putBox(std::vector<uint8_t>& b, const uint8_t* type, const uint8_t* body, size_t len) {
    put32(b, static_cast<uint32_t>(8 + len));
    b.insert(b.end(), type, type + 4);
    b.insert(b.end(), body, body + len);
}

// True when [p, p + n) is exactly a run of whole boxes, each small enough to rewrite with a 32-bit size.
inline bool tiles(const uint8_t* p, size_t n) {
    size_t walked = 0;
    bool small = true;
    forEachBox(p, n, [&](const uint8_t*, const uint8_t* body, size_t len) {
        walked = static_cast<size_t>(body + len - p);
        small = small && len <= UINT32_MAX - 16;
    });
    return small && walked == n;
}

// A trun's data offset, where it sits in the rewritten moof, and the sample bytes it names, as a
// position in the fragment as given.
struct Run {
    size_t field = 0;
    int64_t at = 0;
    uint64_t bytes = 0;
};

// One traf rewritten into out: tfhd's base offset dropped for default-base-is-moof, a tfdt after it
// unless it has one, everything else copied. False on anything it cannot place exactly.
inline bool rewriteTraf(const uint8_t* p, size_t n, Tracks& tracks, int64_t moofAt, bool firstTraf,
                        int64_t* decodeTimes, std::vector<uint8_t>& out, std::vector<Run>& runs) {
    if (!tiles(p, n)) return false;
    uint32_t id = 0;
    uint64_t units = 0;
    readTraf(p, n, tracks, id, units);
    const TrackTiming* track = tracks.find(id);
    if (!track) return false;
    int64_t& decodeTime = decodeTimes[track - tracks.list];
    bool hasTfdt = false;
    forEachBox(p, n, [&](const uint8_t* type, const uint8_t* b, size_t len) {
        if (!isType(type, "tfdt") || len < 8) return;
        hasTfdt = true;
        decodeTime = static_cast<int64_t>(b[0] == 1 && len >= 12 ? be64(b + 4) : be32(b + 4));
    });
    const size_t start = out.size();
    put32(out, 0);
    out.insert(out.end(), {'t', 'r', 'a', 'f'});
    bool ok = true;
    bool haveTfhd = false;
    int64_t base = 0;
    uint32_t defaultSize = track->defaultSize;
    forEachBox(p, n, [&](const uint8_t* type, const uint8_t* b, size_t len) {
        if (!ok) return;
        if (isType(type, "tfhd")) {
            const uint32_t flags = len >= 8 ? be32(b) & 0xFFFFFF : 0;
            const size_t baseLen = (flags & 0x01) ? 8 : 0;
            const size_t sizeAt = 8 + baseLen + ((flags & 0x02) ? 4 : 0) + ((flags & 0x08) ? 4 : 0);
            const size_t need = sizeAt + ((flags & 0x10) ? 4 : 0) + ((flags & 0x20) ? 4 : 0);
            if (haveTfhd || len < 8 || len < need) {
                ok = false;
                return;
            }
            haveTfhd = true;
            if (flags & 0x01) {
                const uint64_t given = be64(b + 8);
                if (given > uint64_t{INT64_MAX >> 1}) {
                    ok = false;
                    return;
                }
                base = static_cast<int64_t>(given);
            } else if ((flags & 0x020000) || firstTraf) {
                base = moofAt;
            } else {
                ok = false;   // after the previous traf's data: not a layout any muxer here writes
                return;
            }
            if (flags & 0x10) defaultSize = be32(b + sizeAt);
            put32(out, static_cast<uint32_t>(8 + len - baseLen));
            out.insert(out.end(), {'t', 'f', 'h', 'd'});
            put32(out, (uint32_t{b[0]} << 24) | ((flags & ~0x01u) | 0x020000u));
            out.insert(out.end(), b + 4, b + 8);
            out.insert(out.end(), b + 8 + baseLen, b + len);
            if (!hasTfdt) {
                put32(out, 20);
                out.insert(out.end(), {'t', 'f', 'd', 't'});
                put32(out, 0x01000000);
                put64(out, static_cast<uint64_t>(decodeTime));
            }
        } else if (isType(type, "trun")) {
            const uint32_t flags = len >= 8 ? be32(b) & 0xFFFFFF : 0;
            const uint64_t count = len >= 8 ? be32(b + 4) : 0;
            size_t stride = 0;
            for (uint32_t bit : {0x100u, 0x200u, 0x400u, 0x800u})
                if (flags & bit) stride += 4;
            const size_t head = 8 + 4 + ((flags & 0x004) ? 4 : 0);
            if (!haveTfhd || !(flags & 0x001) || len < head || (stride && (len - head) / stride < count)) {
                ok = false;
                return;
            }
            Run run;
            run.field = out.size() + 8 + 8;
            run.at = base + static_cast<int32_t>(be32(b + 8)) - moofAt;
            if (flags & 0x200) {
                const size_t sizeAt = head + ((flags & 0x100) ? 4 : 0);
                for (uint64_t i = 0; i < count; ++i) run.bytes += be32(b + sizeAt + i * stride);
            } else {
                run.bytes = count * defaultSize;
            }
            runs.push_back(run);
            putBox(out, type, b, len);
        } else {
            putBox(out, type, b, len);
        }
    });
    if (!ok || !haveTfhd) return false;
    patch32(out, start, static_cast<uint32_t>(out.size() - start));
    decodeTime += static_cast<int64_t>(units);
    return true;
}

}  // namespace detail

// Rewrites one fragment (its moof then its mdat, as core/cast/LiveSegments cuts them) into a self-contained
// HLS segment: see the top of this file. moofAt is the moof's position in the muxer's continuous output
// (what its absolute base offsets count from). decodeTimes holds each init track's running decode time
// (kMaxTracks of them, in init order, from 0): read for the tfdt written, then moved on by this
// fragment's samples, only when the rewrite succeeds. Returns the rewritten length written to out, or 0
// when the input is malformed, a sample would resolve outside the segment, or cap is short (input +
// kMaxGrowth is always enough). The mfhd, and so its sequence number, is copied as it is.
inline size_t rewriteFragment(const uint8_t* init, size_t initSize, int64_t moofAt, const uint8_t* fragment,
                              size_t fragmentSize, int64_t* decodeTimes, uint8_t* out, size_t cap) {
    if (!fragment || !decodeTimes || !out || fragmentSize < 8 || moofAt < 0 || moofAt > (INT64_MAX >> 1)) return 0;
    detail::Tracks tracks = detail::readTracks(init, initSize);
    if (tracks.count == 0) return 0;
    uint64_t moofSize = detail::be32(fragment);
    size_t header = 8;
    if (moofSize == 1) {
        if (fragmentSize < 16) return 0;
        moofSize = detail::be64(fragment + 8);
        header = 16;
    }
    if (!detail::isType(fragment + 4, "moof") || moofSize < header || moofSize > fragmentSize) return 0;
    const uint8_t* body = fragment + header;
    const size_t bodyLen = static_cast<size_t>(moofSize) - header;
    if (!detail::tiles(body, bodyLen)) return 0;

    int64_t times[kMaxTracks];
    std::memcpy(times, decodeTimes, sizeof times);
    std::vector<uint8_t> moof;
    moof.reserve(static_cast<size_t>(moofSize) + kMaxGrowth);
    detail::put32(moof, 0);
    moof.insert(moof.end(), {'m', 'o', 'o', 'f'});
    std::vector<detail::Run> runs;
    int trafs = 0;
    bool ok = true;
    detail::forEachBox(body, bodyLen, [&](const uint8_t* type, const uint8_t* b, size_t len) {
        if (!ok) return;
        if (detail::isType(type, "traf")) {
            ok = ++trafs <= kMaxTracks &&
                 detail::rewriteTraf(b, len, tracks, moofAt, trafs == 1, times, moof, runs);
        } else {
            detail::putBox(moof, type, b, len);
        }
    });
    if (!ok) return 0;
    detail::patch32(moof, 0, static_cast<uint32_t>(moof.size()));
    const auto oldMoof = static_cast<int64_t>(moofSize);
    const auto size = static_cast<int64_t>(fragmentSize);
    for (const detail::Run& run : runs) {
        if (run.at < oldMoof || run.at > size || run.bytes > static_cast<uint64_t>(size - run.at)) return 0;
        const int64_t offset = run.at - oldMoof + static_cast<int64_t>(moof.size());
        if (offset > INT32_MAX) return 0;
        detail::patch32(moof, run.field, static_cast<uint32_t>(offset));
    }
    const size_t rest = fragmentSize - static_cast<size_t>(moofSize);
    if (moof.size() + rest > cap) return 0;
    std::memcpy(out, moof.data(), moof.size());
    std::memcpy(out + moof.size(), fragment + moofSize, rest);
    std::memcpy(decodeTimes, times, sizeof times);
    return moof.size() + rest;
}


}  // namespace geode::viz::frame::hls
