// Host-side checks for core/viz/frame/HlsSegments (#656): the Cast HLS playlist's timing.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: a segment's duration is media time read from the fragment itself (the video track's
// sample durations in its trun, over the timescale its mdhd gives), never how long the encoder took;
// Media3 1.11's real box layout (tfhd base offset, trun v1 0x701, no tfdt) and the tfdt/0xF01 one both
// parse; the trex and tfhd default durations are honoured; without a video track the longest track
// counts; an unreadable fragment is listed at the nominal segment length (the cast keyframe interval);
// TARGETDURATION is fixed per stream from that nominal length (RFC 8216 6.2.1, 4.3.3.1); and the
// playlist text (header, media sequence, init map, one EXTINF per segment, the newest WINDOW only) is
// not served until START segments are listed.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/frame/HlsSegments.hpp"

namespace hls = geode::viz::frame::hls;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

// ---- A tiny ISO BMFF writer, enough to build the boxes FragmentedMp4Muxer writes. ----

using Bytes = std::vector<uint8_t>;

void u32(Bytes& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}
void u64(Bytes& b, uint64_t v) {
    u32(b, static_cast<uint32_t>(v >> 32));
    u32(b, static_cast<uint32_t>(v));
}

Bytes box(const char* type, const Bytes& body) {
    Bytes b;
    u32(b, static_cast<uint32_t>(8 + body.size()));
    b.insert(b.end(), type, type + 4);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

// The same box with a 64-bit largesize, as a muxer may write for a big mdat.
Bytes largeBox(const char* type, const Bytes& body) {
    Bytes b;
    u32(b, 1);
    b.insert(b.end(), type, type + 4);
    u64(b, 16 + body.size());
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

Bytes full(uint8_t version, uint32_t flags) {
    Bytes b;
    u32(b, (static_cast<uint32_t>(version) << 24) | (flags & 0xFFFFFF));
    return b;
}

Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes b;
    for (const Bytes& p : parts) b.insert(b.end(), p.begin(), p.end());
    return b;
}

struct Track {
    uint32_t id;
    uint32_t timescale;
    const char* handler;   // "vide", "soun"
    uint8_t version = 0;   // tkhd and mdhd version
};

Bytes trak(const Track& t) {
    Bytes tkhd = full(t.version, 7);
    if (t.version == 1) {
        u64(tkhd, 0);
        u64(tkhd, 0);
    } else {
        u32(tkhd, 0);
        u32(tkhd, 0);
    }
    u32(tkhd, t.id);
    for (int i = 0; i < 16; ++i) u32(tkhd, 0);   // the rest of tkhd: never read
    Bytes mdhd = full(t.version, 0);
    if (t.version == 1) {
        u64(mdhd, 0);
        u64(mdhd, 0);
    } else {
        u32(mdhd, 0);
        u32(mdhd, 0);
    }
    u32(mdhd, t.timescale);
    u32(mdhd, 0);
    u32(mdhd, 0);
    Bytes hdlr = full(0, 0);
    u32(hdlr, 0);
    hdlr.insert(hdlr.end(), t.handler, t.handler + 4);
    for (int i = 0; i < 3; ++i) u32(hdlr, 0);
    hdlr.push_back(0);
    return box("trak", cat({box("tkhd", tkhd), box("mdia", cat({box("mdhd", mdhd), box("hdlr", hdlr)}))}));
}

Bytes trex(uint32_t id, uint32_t defaultDuration) {
    Bytes b = full(0, 0);
    u32(b, id);
    u32(b, 1);
    u32(b, defaultDuration);
    u32(b, 0);
    u32(b, 0);
    return box("trex", b);
}

// ftyp + moov, as the init segment the muxer writes once.
Bytes init(const std::vector<Track>& tracks, const Bytes& mvex = {}) {
    Bytes ftyp;
    ftyp.insert(ftyp.end(), {'i', 's', 'o', '6', 0, 0, 0, 1});
    Bytes moov = box("mvhd", cat({full(0, 0), Bytes(96, 0)}));
    for (const Track& t : tracks) moov = cat({moov, trak(t)});
    if (!mvex.empty()) moov = cat({moov, box("mvex", mvex)});
    return cat({box("ftyp", ftyp), box("moov", moov)});
}

// A traf in Media3 1.11's real layout (FragmentedMp4Muxer, Boxes.tfhd/trun): tfhd flags 0x000001 with
// an 8-byte base_data_offset (plus 0x08 and a default duration when tfhdDefault is given), no tfdt, and
// trun version 1 with flags 0x701 (data offset, then per sample: duration, size, flags), or 0x601 and
// no durations when durations is empty, so count samples take the tfhd/trex default instead.
Bytes traf(uint32_t id, const std::vector<uint32_t>& durations, uint32_t count = 0, int64_t tfhdDefault = -1) {
    Bytes tfhd = full(0, tfhdDefault >= 0 ? 0x000009 : 0x000001);
    u32(tfhd, id);
    u64(tfhd, 123456);   // base data offset: never read
    if (tfhdDefault >= 0) u32(tfhd, static_cast<uint32_t>(tfhdDefault));
    const bool perSample = !durations.empty();
    Bytes trun = full(1, perSample ? 0x701 : 0x601);
    const uint32_t n = perSample ? static_cast<uint32_t>(durations.size()) : count;
    u32(trun, n);
    u32(trun, 0);   // data offset
    for (uint32_t i = 0; i < n; ++i) {
        if (perSample) u32(trun, durations[i]);
        u32(trun, 100);   // size
        u32(trun, 0);     // flags
    }
    return box("traf", cat({box("tfhd", tfhd), box("trun", trun)}));
}

// The other layout a muxer may write (Media3 with B-frames, or another muxer): tfhd with no base offset
// (0x020000), a tfdt, and trun version 0 with flags 0xF01 (a composition offset per sample as well).
Bytes trafWithTfdt(uint32_t id, const std::vector<uint32_t>& durations) {
    Bytes tfhd = full(0, 0x020000);
    u32(tfhd, id);
    Bytes tfdt = full(1, 0);
    u64(tfdt, 900000);   // a base decode time: segment timing must not depend on it
    Bytes trun = full(0, 0xF01);
    u32(trun, static_cast<uint32_t>(durations.size()));
    u32(trun, 0);   // data offset
    for (uint32_t d : durations) {
        u32(trun, d);
        u32(trun, 100);   // size
        u32(trun, 0);     // flags
        u32(trun, 0);     // composition offset
    }
    return box("traf", cat({box("tfhd", tfhd), box("tfdt", tfdt), box("trun", trun)}));
}

Bytes moof(std::initializer_list<Bytes> trafs) {
    Bytes mfhd = full(0, 0);
    u32(mfhd, 7);
    Bytes body = box("mfhd", mfhd);
    for (const Bytes& t : trafs) body = cat({body, t});
    return box("moof", body);
}

const std::vector<Track> kCast = {{1, 90000, "vide"}, {2, 48000, "soun"}};

std::vector<uint32_t> repeat(uint32_t v, int n) { return std::vector<uint32_t>(static_cast<size_t>(n), v); }

double seconds(const Bytes& i, const Bytes& m) { return hls::fragmentSeconds(i.data(), i.size(), m.data(), m.size()); }

// ---- Segment duration: media time from the fragment ----

void testVideoTrackDurationIsMediaTime() {
    const Bytes i = init(kCast);
    // One second of 30 fps video at 90 kHz, beside 47 AAC frames (1.0027 s) of audio.
    const Bytes m = moof({traf(1, repeat(3000, 30)), traf(2, repeat(1024, 47))});
    check(near(seconds(i, m), 1.0), "a fragment lasts its video samples' durations over the video timescale");
    const Bytes half = moof({traf(1, repeat(3000, 15)), traf(2, repeat(1024, 23))});
    check(near(seconds(i, half), 0.5), "a short fragment is short, however long the encoder took");
}

void testVariableFrameDurationsAddUp() {
    const Bytes i = init(kCast);
    const Bytes m = moof({traf(1, {3000, 6000, 3000, 9000, 1500, 1500})});
    check(near(seconds(i, m), 24000.0 / 90000.0), "per-sample durations add up, uneven ones too");
}

void testDefaultsFromTrexAndTfhd() {
    const Bytes withTrex = init(kCast, cat({trex(1, 3000), trex(2, 1024)}));
    check(near(seconds(withTrex, moof({traf(1, {}, 30)})), 1.0), "trex's default sample duration counts when trun lists none");
    check(near(seconds(withTrex, moof({traf(1, {}, 30, 1500)})), 0.5), "tfhd's default beats trex's");
}

void testTfdtAndCompositionOffsetsLayout() {
    const Bytes i = init(kCast);
    const Bytes m = moof({trafWithTfdt(1, repeat(3000, 30)), trafWithTfdt(2, repeat(1024, 47))});
    check(near(seconds(i, m), 1.0), "a tfdt and per-sample composition offsets change nothing");
}

void testVersionOneHeaders() {
    const Bytes i = init({{1, 600, "vide", 1}});
    check(near(seconds(i, moof({traf(1, repeat(20, 30))})), 1.0), "version 1 tkhd and mdhd are read");
}

void testNoVideoTrackTakesTheLongest() {
    const Bytes i = init({{3, 48000, "soun"}, {4, 44100, "soun"}});
    const Bytes m = moof({traf(3, repeat(1024, 47)), traf(4, repeat(1024, 44))});
    check(near(seconds(i, m), 44.0 * 1024.0 / 44100.0), "with no video track, the longest track is the segment");
}

void testLargeSizeBoxes() {
    // A largesize moov wrapper must parse like the plain one.
    Bytes moovBody = box("mvhd", cat({full(0, 0), Bytes(96, 0)}));
    moovBody = cat({moovBody, trak(kCast[0])});
    const Bytes i = largeBox("moov", moovBody);
    check(near(seconds(i, moof({traf(1, repeat(3000, 30))})), 1.0), "a 64-bit largesize box is read");
}

void testBrokenInputIsZero() {
    const Bytes i = init(kCast);
    const Bytes m = moof({traf(1, repeat(3000, 30))});
    check(seconds(i, Bytes{}) == 0.0, "no fragment, no duration");
    check(hls::fragmentSeconds(nullptr, 0, m.data(), m.size()) == 0.0, "no init, no timescale, no duration");
    Bytes cut(m.begin(), m.begin() + static_cast<long>(m.size() / 2));
    check(seconds(i, cut) >= 0.0, "a truncated fragment never reads past its end");
    Bytes lying = m;
    lying[3] = 0xFF;   // the moof claims more bytes than there are
    check(seconds(i, lying) == 0.0, "a box larger than its parent is ignored");
    check(seconds(init({{1, 0, "vide"}}), m) == 0.0, "a zero timescale gives no duration");
}

// ---- The playlist ----

std::string playlist(int64_t first, const std::vector<double>& durations) {
    return hls::playlist(first, durations.data(), static_cast<int>(durations.size()), "init.mp4", "seg", ".m4s");
}

void testPlaylistWaitsForStartSegments() {
    check(playlist(0, {}).empty(), "no playlist before any segment");
    check(playlist(0, {1.0, 1.0}).empty(), "no playlist until the receiver has START segments to start on");
    check(!playlist(0, {1.0, 1.0, 1.0}).empty(), "a playlist once START segments are listed");
    check(hls::kStartSegments == GEODE_HLS_START_SEGMENTS && hls::kWindow == GEODE_HLS_WINDOW, "the ABI's limits are the engine's");
}

void testPlaylistText() {
    const std::string p = playlist(41, {1.0, 0.98, 1.0333333});
    const std::string want =
        "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:41\n"
        "#EXT-X-MAP:URI=\"init.mp4\"\n"
        "#EXTINF:1.000,\nseg41.m4s\n#EXTINF:0.980,\nseg42.m4s\n#EXTINF:1.033,\nseg43.m4s\n";
    check(p == want, "the playlist names the init map and each segment with its media-time EXTINF");
    if (p != want) std::printf("got:\n%s\n", p.c_str());
}

void testTargetDurationIsFixedPerStream() {
    // RFC 8216 6.2.1: TARGETDURATION never changes within a stream; 4.3.3.1: each EXTINF rounded to the
    // nearest integer is at most it. So it is the nominal segment rounded, never the window's longest.
    check(hls::targetDuration(1.0) == 1, "one-second fragments target 1");
    check(hls::targetDuration(0.2) == 1, "never below 1");
    check(hls::targetDuration(2.0) == 2 && hls::targetDuration(2.4) == 2, "rounded to the nearest second");
    const std::string calm = playlist(0, {1.0, 1.0, 1.0});
    const std::string late = playlist(1, {1.0, 1.0333, 1.0});
    const std::string slow = playlist(2, {1.0333, 1.0, 1.4});
    const char* one = "#EXT-X-TARGETDURATION:1\n";
    check(calm.find(one) != std::string::npos && late.find(one) != std::string::npos && slow.find(one) != std::string::npos,
          "a late keyframe (1.033 s) or a slow fragment never moves the target between reloads");
}

void testUnknownDurationIsTheNominalSegment() {
    const std::string p = playlist(0, {0.0, 1.0, 1.0});
    check(p.find("#EXTINF:1.000,\nseg0.m4s") != std::string::npos, "a fragment that could not be read is listed at the nominal segment length");
    check(hls::kNominalSegmentSec == geode::viz::frame::CastFormat::kKeyframeIntervalSec, "the nominal segment is the cast keyframe interval");
}

void testPlaylistListsOnlyTheNewestWindow() {
    std::vector<double> d(static_cast<size_t>(hls::kWindow + 2), 1.0);
    const std::string p = playlist(10, d);
    check(p.find("#EXT-X-MEDIA-SEQUENCE:12\n") != std::string::npos, "more than the window: the oldest are dropped");
    check(p.find("seg11.m4s") == std::string::npos && p.find("seg17.m4s") != std::string::npos, "and the newest kept");
}

// ---- The C ABI, as LiveSegments calls it ----

void testAbi() {
    const Bytes i = init(kCast);
    const Bytes m = moof({traf(1, repeat(3000, 30)), traf(2, repeat(1024, 47))});
    check(near(geode_hls_fragment_seconds(i.data(), i.size(), m.data(), m.size()), 1.0), "geode_hls_fragment_seconds reads media time");
    const double d[] = {1.0, 1.0, 1.0};
    char out[1024];
    const int n = geode_hls_playlist(5, d, 3, "init.mp4", "seg", ".m4s", out, sizeof out);
    check(n > 0 && std::string(out, static_cast<size_t>(n)) == playlist(5, {1.0, 1.0, 1.0}), "geode_hls_playlist writes the text");
    check(out[n] == '\0', "and terminates it");
    check(geode_hls_playlist(5, d, 2, "init.mp4", "seg", ".m4s", out, sizeof out) == 0, "0 while not ready");
    char small[16];
    const int needed = geode_hls_playlist(5, d, 3, "init.mp4", "seg", ".m4s", small, sizeof small);
    check(needed == n && small[0] == '\0', "too small a buffer: nothing written, the length it needs returned");
    check(geode_hls_playlist(5, d, 3, "init.mp4", "seg", ".m4s", nullptr, 0) == n, "a size query with no buffer");
    check(geode_hls_window() == hls::kWindow, "Kotlin reads the window from native");
    check(geode_hls_playlist(5, nullptr, 3, "init.mp4", "seg", ".m4s", out, sizeof out) == 0, "0 without durations");
}

}  // namespace

int main() {
    testVideoTrackDurationIsMediaTime();
    testVariableFrameDurationsAddUp();
    testDefaultsFromTrexAndTfhd();
    testTfdtAndCompositionOffsetsLayout();
    testVersionOneHeaders();
    testNoVideoTrackTakesTheLongest();
    testLargeSizeBoxes();
    testBrokenInputIsZero();
    testPlaylistWaitsForStartSegments();
    testPlaylistText();
    testTargetDurationIsFixedPerStream();
    testUnknownDurationIsTheNominalSegment();
    testPlaylistListsOnlyTheNewestWindow();
    testAbi();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("hls_segments_test: all passed\n");
    return 0;
}
