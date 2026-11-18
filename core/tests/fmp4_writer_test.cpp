// Host test for cast/Fmp4Writer: the cast's fragmented MP4 written from the encoders' own buffers. The init
// segment's box tree (the tracks, timescales, kinds and codecs the HLS and browser paths read back), each
// fragment's moof + mdat (sizes that add up, mfhd sequence numbers from 1, tfdt decode times, trun data offsets
// pointing at each sample's own bytes, sync flags), cuts on the first keyframe a fragment length on, Annex B to
// length-prefixed NAL units, samples held until both configs are known, video before the first keyframe
// dropped, PTS that go back, and the whole stream through cast/LiveSegments with nothing served as muxed.
//
// Build & run:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/fmp4_writer_test.cpp
//       core/cast/Fmp4Writer.cpp core/cast/LiveSegments.cpp core/viz/frame/LinkShare.cpp -o fmp4_writer_test
//   ./fmp4_writer_test
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cast/Fmp4Writer.hpp"
#include "cast/LiveSegments.hpp"
#include "viz/frame/HlsSegments.hpp"
#include "viz/frame/LinkShare.hpp"

namespace hls = geode::viz::frame::hls;
namespace share = geode::viz::frame::link;
using geode::cast::Fmp4Writer;
using geode::cast::LiveSegments;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

using Buf = std::vector<uint8_t>;

uint32_t be32(const uint8_t* p) { return hls::detail::be32(p); }
uint64_t be64(const uint8_t* p) { return hls::detail::be64(p); }

// The child box of [p, p + n) with this type: its body and length; null when there is none.
const uint8_t* child(const uint8_t* p, size_t n, const char* type, size_t* len) {
    const uint8_t* found = nullptr;
    hls::detail::forEachBox(p, n, [&](const uint8_t* t, const uint8_t* b, size_t l) {
        if (!found && std::memcmp(t, type, 4) == 0) {
            found = b;
            *len = l;
        }
    });
    return found;
}

// The top-level boxes of a buffer, as types, and true when they tile it exactly.
std::vector<std::string> topTypes(const Buf& b, bool* tiles) {
    std::vector<std::string> out;
    size_t walked = 0;
    hls::detail::forEachBox(b.data(), b.size(), [&](const uint8_t* t, const uint8_t* body, size_t len) {
        out.emplace_back(reinterpret_cast<const char*>(t), 4);
        walked = static_cast<size_t>(body + len - b.data());
    });
    *tiles = walked == b.size();
    return out;
}

// A Baseline 3.0 SPS and a PPS (headers only; nothing here decodes them), with a delimiter in front.
const Buf kConfig = {0, 0, 0, 1, 0x09, 0xF0, 0, 0, 0, 1, 0x67, 0x42, 0xC0, 0x1E, 0xDA, 0x01,
                     0, 0, 1, 0x68, 0xCE, 0x3C, 0x80};
const Buf kAsc = {0x11, 0x90};   // AAC LC, 48 kHz, stereo

// One access unit: a 4-byte start code, a slice NAL of the given type and a marker byte, then a 3-byte start
// code and an SEI (kept: only parameter sets and delimiters go).
Buf accessUnit(bool key, uint8_t marker) {
    return {0, 0, 0, 1, static_cast<uint8_t>(key ? 0x65 : 0x41), marker, 0xAA, 0, 0, 1, 0x06, marker};
}

struct Out {
    std::vector<Buf> writes;
};

Fmp4Writer makeWriter(Out& out) {
    Fmp4Writer::Tracks tracks;
    tracks.width = 1280;
    tracks.height = 720;
    tracks.audioRateHz = 48'000;
    tracks.channels = 2;
    tracks.audioBitRate = 128'000;
    return Fmp4Writer(tracks, 1'000'000, [&out](const uint8_t* d, size_t n) { out.writes.emplace_back(d, d + n); });
}

void testNalUnits() {
    const Buf au = accessUnit(true, 7);
    const auto units = Fmp4Writer::nalUnits(au.data(), au.size());
    check(units.size() == 2, "two NAL units in an access unit");
    check(units.size() == 2 && units[0].second == 3 && units[0].first[0] == 0x65, "the slice NAL, no start code");
    check(units.size() == 2 && units[1].second == 2 && units[1].first[0] == 0x06, "the SEI after a 3-byte code");
    const Buf prefixed = Fmp4Writer::lengthPrefixed(au.data(), au.size());
    const Buf want = {0, 0, 0, 3, 0x65, 7, 0xAA, 0, 0, 0, 2, 0x06, 7};
    check(prefixed == want, "length-prefixed NAL units");
    const Buf config = Fmp4Writer::lengthPrefixed(kConfig.data(), kConfig.size());
    check(config.empty(), "parameter sets and delimiters leave the samples");
    const Buf bare = {0x41, 1, 2};
    const auto one = Fmp4Writer::nalUnits(bare.data(), bare.size());
    check(one.size() == 1 && one[0].second == 3, "no start code: one unit");
    const Buf trailing = {0, 0, 1, 0x41, 5, 0, 0};
    const auto trimmed = Fmp4Writer::nalUnits(trailing.data(), trailing.size());
    check(trimmed.size() == 1 && trimmed[0].second == 2, "trailing zero bytes are not the NAL's");
}

void testConfigs() {
    Out out;
    Fmp4Writer w = makeWriter(out);
    const Buf noPps = {0, 0, 0, 1, 0x67, 0x42, 0xC0, 0x1E};
    check(!w.videoConfig(noPps.data(), noPps.size()), "an SPS without a PPS is refused");
    Buf longAsc(65, 1);
    check(!w.audioConfig(longAsc.data(), longAsc.size()), "an AudioSpecificConfig past one length byte is refused");
    check(!w.audioConfig(nullptr, 0), "an empty AudioSpecificConfig is refused");
    check(w.videoConfig(kConfig.data(), kConfig.size()), "SPS and PPS taken");
    check(out.writes.empty() && !w.started(), "nothing written with one config");
    check(w.audioConfig(kAsc.data(), kAsc.size()), "the AudioSpecificConfig taken");
    check(w.started() && out.writes.size() == 1, "the init segment once both are known");
}

void testInitSegment(const Buf& init) {
    bool tiles = false;
    const auto types = topTypes(init, &tiles);
    check(tiles && types.size() == 2 && types[0] == "ftyp" && types[1] == "moov", "init: ftyp then moov");
    const hls::detail::Tracks tracks = hls::detail::readTracks(init.data(), init.size());
    check(tracks.count == 2, "init: two tracks");
    if (tracks.count == 2) {
        check(tracks.list[0].id == 1 && tracks.list[0].video && tracks.list[0].timescale == 90'000, "init: video track");
        check(tracks.list[1].id == 2 && !tracks.list[1].video && tracks.list[1].timescale == 48'000, "init: audio track");
    }
    check(share::codecs(init.data(), init.size()) == "avc1.42c01e,mp4a.40.2", "init: the codecs a browser is told");
    size_t moovLen = 0;
    const uint8_t* moov = child(init.data(), init.size(), "moov", &moovLen);
    size_t mvexLen = 0;
    const uint8_t* mvex = moov ? child(moov, moovLen, "mvex", &mvexLen) : nullptr;
    check(mvex != nullptr, "init: mvex, so fragments may follow");
    size_t mvhdLen = 0;
    const uint8_t* mvhd = moov ? child(moov, moovLen, "mvhd", &mvhdLen) : nullptr;
    check(mvhd && mvhdLen == 100 && be32(mvhd + 96) == 3, "init: mvhd's size and next track id");
}

struct Traf {
    uint32_t track = 0;
    uint32_t tfhdFlags = 0;
    uint64_t decode = 0;
    uint32_t count = 0;
    uint32_t dataOffset = 0;
    std::vector<uint32_t> durations, sizes, flags;
};

std::vector<Traf> readFragment(const Buf& f, uint32_t* sequence, bool* ok) {
    std::vector<Traf> out;
    *ok = false;
    bool tiles = false;
    const auto types = topTypes(f, &tiles);
    if (!tiles || types.size() != 2 || types[0] != "moof" || types[1] != "mdat") return out;
    const size_t moofLen = be32(f.data()) - 8;
    const uint8_t* moof = f.data() + 8;
    size_t mfhdLen = 0;
    const uint8_t* mfhd = child(moof, moofLen, "mfhd", &mfhdLen);
    if (!mfhd || mfhdLen != 8) return out;
    *sequence = be32(mfhd + 4);
    hls::detail::forEachBox(moof, moofLen, [&](const uint8_t* t, const uint8_t* b, size_t l) {
        if (std::memcmp(t, "traf", 4) != 0) return;
        Traf traf;
        size_t len = 0;
        const uint8_t* tfhd = child(b, l, "tfhd", &len);
        if (tfhd && len == 8) {
            traf.tfhdFlags = be32(tfhd) & 0xFFFFFF;
            traf.track = be32(tfhd + 4);
        }
        const uint8_t* tfdt = child(b, l, "tfdt", &len);
        if (tfdt && len == 12 && tfdt[0] == 1) traf.decode = be64(tfdt + 4);
        const uint8_t* trun = child(b, l, "trun", &len);
        if (trun && len >= 12 && (be32(trun) & 0xFFFFFF) == 0x701) {
            traf.count = be32(trun + 4);
            traf.dataOffset = be32(trun + 8);
            for (uint32_t i = 0; i < traf.count && 12 + 12 * (i + 1) <= len; ++i) {
                traf.durations.push_back(be32(trun + 12 + 12 * i));
                traf.sizes.push_back(be32(trun + 16 + 12 * i));
                traf.flags.push_back(be32(trun + 20 + 12 * i));
            }
        }
        out.push_back(traf);
    });
    *ok = true;
    return out;
}

// 30 fps video, a keyframe every 30 frames, AAC frames on 48 kHz, interleaved as a drain would hand them over;
// some samples come before the configs and two non-keyframes before the first keyframe.
void testStream() {
    Out out;
    Fmp4Writer w = makeWriter(out);
    const int64_t frameUs = 33'333;
    const int64_t aacUs = 1024 * 1'000'000LL / 48'000;   // 21333 us

    // Before either config: held, then fed in order. The first two video frames are not keyframes.
    const Buf a0 = {0x21, 0, 0};
    w.audio(a0.data(), a0.size(), 0);
    const Buf early = accessUnit(false, 0xEE);
    w.video(early.data(), early.size(), 0, false);
    w.video(early.data(), early.size(), frameUs, false);
    w.videoConfig(kConfig.data(), kConfig.size());
    check(out.writes.empty(), "samples held while a config is missing");
    w.audioConfig(kAsc.data(), kAsc.size());
    check(out.writes.size() == 1, "only the init segment once the configs are known");
    check(w.dropped() == 2, "video before the first keyframe dropped");
    testInitSegment(out.writes[0]);

    int64_t audioPts = aacUs;
    const int frames = 95;   // three whole fragments and a few frames of the fourth
    for (int i = 2; i < frames + 2; ++i) {
        const int64_t pts = i * frameUs;
        while (audioPts <= pts) {
            const Buf a = {0x21, static_cast<uint8_t>(audioPts / aacUs), 0x55};
            w.audio(a.data(), a.size(), audioPts);
            audioPts += aacUs;
        }
        const bool key = (i - 2) % 30 == 0;
        const Buf v = accessUnit(key, static_cast<uint8_t>(i));
        w.video(v.data(), v.size(), pts, key);
    }
    check(out.writes.size() == 4, "three fragments cut on the keyframes");
    w.flush();
    check(out.writes.size() == 5 && w.fragments() == 4, "the open fragment written at the end");

    uint64_t lastVideoEnd = 0;
    uint64_t lastAudioEnd = 0;
    for (size_t k = 1; k < out.writes.size(); ++k) {
        const Buf& f = out.writes[k];
        uint32_t sequence = 0;
        bool ok = false;
        const auto trafs = readFragment(f, &sequence, &ok);
        check(ok, "a fragment is a moof then an mdat, nothing else");
        check(sequence == k, "mfhd sequence numbers count from 1");
        check(trafs.size() == 2 && trafs[0].track == 1 && trafs[1].track == 2, "video traf then audio traf");
        if (trafs.size() != 2) continue;
        const size_t mdatAt = be32(f.data());
        size_t payload = 0;
        for (const Traf& t : trafs) {
            check(t.tfhdFlags == 0x020000, "tfhd: default-base-is-moof alone");
            check(t.sizes.size() == t.count, "trun lists every sample");
            for (uint32_t s : t.sizes) payload += s;
        }
        check(be32(f.data() + mdatAt) == 8 + payload && f.size() == mdatAt + 8 + payload, "mdat holds exactly the samples");
        const Traf& v = trafs[0];
        const Traf& a = trafs[1];
        check(v.dataOffset == mdatAt + 8, "video data starts the mdat");
        check(a.dataOffset == v.dataOffset + [&] {
                  uint32_t n = 0;
                  for (uint32_t s : v.sizes) n += s;
                  return n;
              }(),
              "audio data follows the video");
        // The first video sample's bytes, where the offset says: length 3, a keyframe slice.
        check(v.dataOffset + 5 <= f.size() && be32(f.data() + v.dataOffset) == 3 && f[v.dataOffset + 4] == 0x65,
              "the first sample is a keyframe's length-prefixed slice");
        check(!v.flags.empty() && v.flags[0] == Fmp4Writer::kSyncFlags, "the first video sample is sync");
        bool restNonSync = true;
        for (size_t i = 1; i < v.flags.size(); ++i) restNonSync = restNonSync && v.flags[i] == Fmp4Writer::kNonSyncFlags;
        check(restNonSync, "the rest are not");
        check(k == 1 || v.decode == lastVideoEnd, "video decode times run on with no gap");
        uint64_t sum = 0;
        for (uint32_t d : v.durations) sum += d;
        lastVideoEnd = v.decode + sum;
        check(k == 1 || a.decode >= lastAudioEnd, "audio never goes back");
        uint64_t asum = 0;
        for (uint32_t d : a.durations) asum += d;
        lastAudioEnd = a.decode + asum;
        bool aacFrames = true;
        for (uint32_t d : a.durations) aacFrames = aacFrames && d == 1024;
        check(aacFrames, "each AAC frame lasts 1024 samples");
        if (k < 4) {
            check(v.count == 30, "a whole fragment holds 30 frames");
            const double sec = hls::fragmentSeconds(out.writes[0].data(), out.writes[0].size(), f.data(), f.size());
            check(sec > 0.999 && sec < 1.001, "a whole fragment plays one second");
        }
    }
    // The origin is the first sample kept (the held audio at 0); the first keyframe is two frames on.
    uint32_t seq = 0;
    bool ok = false;
    const auto first = readFragment(out.writes[1], &seq, &ok);
    check(ok && first.size() == 2 && first[0].decode == uint64_t{66'666} * 9 / 100, "video's first decode time from the origin");
    check(ok && first.size() == 2 && first[1].decode == 0, "audio starts at the origin");

    // The whole stream through the segment cutter: every fragment rewritten, none served as muxed.
    LiveSegments segments([] { return int64_t{0}; });
    for (const Buf& b : out.writes) segments.write(b.data(), b.size());
    check(!segments.broken(), "LiveSegments takes the stream");
    check(segments.init() && *segments.init() == out.writes[0], "the init segment as written");
    check(segments.servedAsMuxed() == 0, "every fragment rewrites cleanly");
    check(!segments.playlist().empty(), "a playlist once three segments are here");
    check(segments.segment(0) && segments.segment(3), "four segments kept");
    check(segments.masterPlaylist().find("avc1.42c01e,mp4a.40.2") != std::string::npos, "the master playlist's codecs");
}

void testTimeGoesForward() {
    Out out;
    Fmp4Writer w = makeWriter(out);
    w.videoConfig(kConfig.data(), kConfig.size());
    w.audioConfig(kAsc.data(), kAsc.size());
    const Buf key = accessUnit(true, 1);
    const Buf delta = accessUnit(false, 2);
    w.video(key.data(), key.size(), 1'000'000, true);
    w.video(delta.data(), delta.size(), 1'000'000, false);   // the same PTS
    w.video(delta.data(), delta.size(), 900'000, false);     // back
    w.video(delta.data(), delta.size(), 1'100'000, false);
    const Buf a = {1, 2};
    w.audio(a.data(), a.size(), 1'000'000);
    w.audio(a.data(), a.size(), 1'000'000);
    w.flush();
    check(out.writes.size() == 2, "one fragment");
    uint32_t seq = 0;
    bool ok = false;
    const auto trafs = readFragment(out.writes.back(), &seq, &ok);
    check(ok && trafs.size() == 2, "video and audio trafs");
    if (!ok || trafs.size() != 2) return;
    const auto& d = trafs[0].durations;
    check(d.size() == 4 && d[0] == 1 && d[1] == 1 && d[2] == 9'000 - 2, "a PTS that does not move on is one tick on");
    check(trafs[1].durations.size() == 2 && trafs[1].durations[0] == 1024, "audio one AAC frame on");
    // The empty sample is never a sample.
    w.video(nullptr, 0, 0, false);
    w.audio(nullptr, 0, 0);
    w.flush();
    check(out.writes.size() == 2, "nothing to flush, nothing written");
}

void testHeldLimit() {
    Out out;
    Fmp4Writer w = makeWriter(out);
    const Buf a = {1};
    for (size_t i = 0; i < Fmp4Writer::kMaxHeld + 10; ++i) w.audio(a.data(), a.size(), static_cast<int64_t>(i) * 21'333);
    check(w.dropped() == 10, "what waits for the configs is bounded");
}

}  // namespace

int main() {
    testNalUnits();
    testConfigs();
    testStream();
    testTimeGoesForward();
    testHeldLimit();
    if (failures == 0) std::printf("fmp4_writer_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
