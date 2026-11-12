// Host test for cast/LiveSegments: the cast encoder's one continuous fragmented MP4 cut into the init segment
// and one media segment per moof + mdat, however the muxer's writes are split; each fragment rewritten into a
// self-contained segment at the right place in the file; the window of segments kept; the exact playlist text
// a TV is served; which fragment a browser's feed gets next; the freeze watchdog's clock; and boxes no stream
// could hold (too small, too large, endless), which end the cutting with nothing kept.
//
// Build & run:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/live_segments_test.cpp
//       core/cast/LiveSegments.cpp core/viz/frame/LinkShare.cpp -o live_segments_test && ./live_segments_test
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "cast/LiveSegments.hpp"
#include "viz/frame/HlsSegments.hpp"
#include "viz/frame/LinkShare.hpp"

namespace hls = geode::viz::frame::hls;
namespace share = geode::viz::frame::link;
using geode::cast::LiveSegments;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// ---- A tiny ISO BMFF writer, enough to build the boxes a fragmented MP4 muxer writes. ----

using Buf = std::vector<uint8_t>;

void u32(Buf& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}
void u64(Buf& b, uint64_t v) {
    u32(b, static_cast<uint32_t>(v >> 32));
    u32(b, static_cast<uint32_t>(v));
}

Buf box(const char* type, const Buf& body) {
    Buf b;
    u32(b, static_cast<uint32_t>(8 + body.size()));
    b.insert(b.end(), type, type + 4);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

// A box header alone, claiming size bytes.
Buf header(uint32_t size, const char* type) {
    Buf b;
    u32(b, size);
    b.insert(b.end(), type, type + 4);
    return b;
}

Buf full(uint8_t version, uint32_t flags) {
    Buf b;
    u32(b, (static_cast<uint32_t>(version) << 24) | (flags & 0xFFFFFF));
    return b;
}

Buf cat(std::initializer_list<Buf> parts) {
    Buf b;
    for (const Buf& p : parts) b.insert(b.end(), p.begin(), p.end());
    return b;
}

// ftyp, and a moov with one video track (id 1, 90 kHz).
Buf ftyp() { return box("ftyp", Buf{'i', 's', 'o', '6', 0, 0, 0, 1}); }

Buf moov() {
    Buf tkhd = full(0, 7);
    u32(tkhd, 0);
    u32(tkhd, 0);
    u32(tkhd, 1);
    for (int i = 0; i < 16; ++i) u32(tkhd, 0);
    Buf mdhd = full(0, 0);
    u32(mdhd, 0);
    u32(mdhd, 0);
    u32(mdhd, 90000);
    u32(mdhd, 0);
    u32(mdhd, 0);
    Buf hdlr = full(0, 0);
    u32(hdlr, 0);
    hdlr.insert(hdlr.end(), {'v', 'i', 'd', 'e'});
    for (int i = 0; i < 3; ++i) u32(hdlr, 0);
    hdlr.push_back(0);
    const Buf trak = box("trak", cat({box("tkhd", tkhd), box("mdia", cat({box("mdhd", mdhd), box("hdlr", hdlr)}))}));
    return box("moov", cat({box("mvhd", cat({full(0, 0), Buf(96, 0)})), trak}));
}

// A moof in the muxer's layout, its start at moofAt in the continuous file: tfhd flags 0x000001 with that
// absolute base data offset and no tfdt, trun version 1 flags 0x701 whose data offset counts from moofAt into
// the mdat that follows. `count` samples of `duration` (90 kHz units) and 40 bytes each.
Buf moof(int64_t moofAt, uint32_t sequence, uint32_t duration, uint32_t count) {
    const auto build = [&](uint32_t dataOffset) {
        Buf mfhd = full(0, 0);
        u32(mfhd, sequence);
        Buf tfhd = full(0, 0x000001);
        u32(tfhd, 1);
        u64(tfhd, static_cast<uint64_t>(moofAt));
        Buf trun = full(1, 0x701);
        u32(trun, count);
        u32(trun, dataOffset);
        for (uint32_t i = 0; i < count; ++i) {
            u32(trun, duration);
            u32(trun, 40);
            u32(trun, 0x02000000);
        }
        return box("moof", cat({box("mfhd", mfhd), box("traf", cat({box("tfhd", tfhd), box("trun", trun)}))}));
    };
    return build(static_cast<uint32_t>(build(0).size() + 8));
}

Buf mdat(uint32_t count) { return box("mdat", Buf(static_cast<size_t>(count) * 40, 0xA1)); }

// The muxer's file as it grows: each fragment's moof knows where it starts.
struct File {
    Buf bytes;

    void add(const Buf& b) { bytes.insert(bytes.end(), b.begin(), b.end()); }
    // One fragment of `count` samples of `duration`; returns it as written (moof + mdat).
    Buf fragment(uint32_t duration, uint32_t count = 30) {
        const Buf f = cat({moof(static_cast<int64_t>(bytes.size()), sequence++, duration, count), mdat(count)});
        add(f);
        return f;
    }

    uint32_t sequence = 1;
};

int64_t zero() { return 0; }

void writeIn(LiveSegments& s, const Buf& bytes, size_t step) {
    for (size_t at = 0; at < bytes.size(); at += step) s.write(bytes.data() + at, std::min(step, bytes.size() - at));
}

Buf held(const geode::cast::Bytes& b) { return b ? *b : Buf{}; }

// ---- Tests ----

void testNothingBeforeTheHeader() {
    LiveSegments s(zero);
    check(!s.init() && s.playlist().empty() && s.masterPlaylist().empty(), "nothing to serve before the muxer writes");
    check(s.msSinceLastSegment() == 0, "the watchdog reads 0 before anything was cut");
    const Buf f = ftyp();
    s.write(f.data(), f.size());
    check(!s.init(), "ftyp alone is not an init segment");
    s.write(nullptr, 10);
    s.write(f.data(), 0);
    check(!s.broken(), "an empty write is nothing");
    LiveSegments::Next next;
    check(!s.nextAfter(-1, next) && !s.segment(0), "no fragment yet");
}

void testCutsTheSameHoweverTheWritesAreSplit() {
    File file;
    file.add(ftyp());
    file.add(moov());
    const size_t initSize = file.bytes.size();
    file.add(box("free", Buf(13, 0)));   // a box between: skipped, but it moves every later moof
    for (int i = 0; i < 4; ++i) file.fragment(3000);

    LiveSegments whole(zero);
    whole.write(file.bytes.data(), file.bytes.size());
    check(held(whole.init()) == Buf(file.bytes.begin(), file.bytes.begin() + static_cast<std::ptrdiff_t>(initSize)),
          "the init segment is ftyp + moov, byte for byte");
    check(whole.servedAsMuxed() == 0, "every fragment was rewritten: each moof was placed where it sits in the file");
    check(!whole.playlist().empty() && whole.segment(3) && !whole.segment(4), "four segments, 0 to 3");

    for (size_t step : {size_t{1}, size_t{7}, size_t{4096}}) {
        LiveSegments pieces(zero);
        writeIn(pieces, file.bytes, step);
        bool same = held(pieces.init()) == held(whole.init()) && pieces.playlist() == whole.playlist();
        for (int64_t q = 0; q < 4; ++q) same = same && pieces.segment(q) && held(pieces.segment(q)) == held(whole.segment(q));
        check(same && pieces.servedAsMuxed() == 0, "the same init, segments and playlist whatever the write sizes");
    }
}

void testSegmentsAreSelfContained() {
    File file;
    file.add(ftyp());
    file.add(moov());
    const Buf first = file.fragment(3000);
    LiveSegments s(zero);
    s.write(file.bytes.data(), file.bytes.size());
    const Buf seg = held(s.segment(0));
    // The tfdt added (20 bytes) and the 8-byte base data offset dropped.
    check(seg.size() == first.size() + 12, "the segment kept is the rewritten fragment, not the muxer's");
    const Buf init = held(s.init());
    int64_t times[hls::kMaxTracks] = {};
    Buf want(first.size() + hls::kMaxGrowth);
    want.resize(hls::rewriteFragment(init.data(), init.size(), static_cast<int64_t>(init.size()), first.data(), first.size(),
                                     times, want.data(), want.size()));
    check(seg == want, "exactly what the rewrite gives for this moof position");
}

void testPlaylistText() {
    File file;
    file.add(ftyp());
    file.add(moov());
    LiveSegments s(zero);
    s.write(file.bytes.data(), file.bytes.size());
    check(s.playlist().empty(), "no playlist with the header alone");
    Buf f = file.fragment(3000);
    s.write(f.data(), f.size());
    f = file.fragment(2940);
    s.write(f.data(), f.size());
    check(s.playlist().empty(), "no playlist until a receiver has three segments to start on");
    f = file.fragment(3100);
    s.write(f.data(), f.size());
    const std::string want =
        "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
        "#EXT-X-MAP:URI=\"init.mp4\"\n"
        "#EXTINF:1.000,\nseg0.m4s\n#EXTINF:0.980,\nseg1.m4s\n#EXTINF:1.033,\nseg2.m4s\n";
    const std::string got = s.playlist();
    check(got == want, "the playlist: the init map and each segment with its media time");
    if (got != want) std::printf("got:\n%s\n", got.c_str());
    check(share::kInitName == "init.mp4" && share::kSegmentPrefix == "seg" && share::kSegmentSuffix == ".m4s",
          "the names the playlist writes are the ones the server routes");
}

void testWindow() {
    File file;
    file.add(ftyp());
    file.add(moov());
    for (int i = 0; i < 8; ++i) file.fragment(3000);
    LiveSegments s(zero);
    s.write(file.bytes.data(), file.bytes.size());
    check(hls::kWindow == 6, "the window is six segments");
    check(!s.segment(0) && !s.segment(1) && s.segment(2) && s.segment(7) && !s.segment(8), "only the newest six are kept");
    const std::string want =
        "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:2\n"
        "#EXT-X-MAP:URI=\"init.mp4\"\n"
        "#EXTINF:1.000,\nseg2.m4s\n#EXTINF:1.000,\nseg3.m4s\n#EXTINF:1.000,\nseg4.m4s\n"
        "#EXTINF:1.000,\nseg5.m4s\n#EXTINF:1.000,\nseg6.m4s\n#EXTINF:1.000,\nseg7.m4s\n";
    check(s.playlist() == want, "the playlist slides with the window");
    check(!s.segment(-1) && !s.segment(INT64_MAX) && !s.segment(INT64_MIN), "any other sequence number is nothing");
}

void testFeedOrder() {
    File file;
    file.add(ftyp());
    file.add(moov());
    for (int i = 0; i < 8; ++i) file.fragment(3000);
    LiveSegments s(zero);
    s.write(file.bytes.data(), file.bytes.size());
    LiveSegments::Next next;
    check(s.nextAfter(-1, next) && next.sequence == 7 && next.bytes == s.segment(7), "a new feed starts at the live edge");
    check(!s.nextAfter(7, next) && !s.nextAfter(99, next), "nothing after the newest");
    check(s.nextAfter(4, next) && next.sequence == 5, "a feed in step gets the next one");
    check(s.nextAfter(0, next) && next.sequence == 7, "a feed that fell out of the window jumps to the newest");
    check(s.nextAfter(1, next) && next.sequence == 2, "the oldest kept is still in step");
    check(s.nextAfter(INT64_MIN, next) && next.sequence == 7, "any negative is a new feed");
    check(!s.nextAfter(INT64_MAX, next), "and the largest sequence has nothing after it");
}

int64_t clockMs = 0;
int64_t fakeNow() { return clockMs; }

void testWatchdogClock() {
    File file;
    LiveSegments s(fakeNow);
    clockMs = 5'000;
    check(s.msSinceLastSegment() == 0, "0 until something was cut, whatever the clock reads");
    file.add(ftyp());
    file.add(moov());
    s.write(file.bytes.data(), file.bytes.size());
    clockMs = 5'250;
    check(s.msSinceLastSegment() == 250, "counted from the header");
    const Buf f = file.fragment(3000);
    clockMs = 6'000;
    s.write(f.data(), f.size() - 1);
    clockMs = 6'400;
    check(s.msSinceLastSegment() == 1'400, "a fragment still arriving is not a cut");
    s.write(f.data() + f.size() - 1, 1);
    clockMs = 7'000;
    check(s.msSinceLastSegment() == 600, "counted from the last fragment's last byte");
}

void testFragmentThatCannotBeRewritten() {
    File file;
    file.add(ftyp());
    file.add(moov());
    LiveSegments s(zero);
    s.write(file.bytes.data(), file.bytes.size());
    const Buf odd = cat({box("moof", Buf{1, 2, 3}), box("mdat", Buf(9, 0x55))});
    s.write(odd.data(), odd.size());
    check(s.servedAsMuxed() == 1 && held(s.segment(0)) == odd, "kept as the muxer wrote it");
    const Buf stray = box("mdat", Buf(5, 0));
    s.write(stray.data(), stray.size());
    check(!s.segment(1), "an mdat with no moof before it is no segment");
    for (int i = 0; i < 2; ++i) {
        const Buf again = cat({box("moof", Buf{1, 2, 3}), box("mdat", Buf(9, 0x55))});
        s.write(again.data(), again.size());
    }
    check(s.playlist().find("#EXTINF:1.000,\nseg0.m4s\n") != std::string::npos, "and listed at the nominal length");
}

void testLargeSizeBox() {
    File file;
    file.add(ftyp());
    file.add(moov());
    LiveSegments s(zero);
    s.write(file.bytes.data(), file.bytes.size());
    Buf big = header(1, "mdat");
    u64(big, 16 + 100);
    big.insert(big.end(), 100, 0x33);
    const Buf f = cat({box("moof", Buf{0, 0, 0, 0}), big});
    writeIn(s, f, 5);
    check(!s.broken() && held(s.segment(0)) == f, "a box with a 64-bit size is cut at its end");
}

void testBoxesNoStreamHolds() {
    const Buf good = cat({ftyp(), moov()});
    {
        LiveSegments s(zero);
        const Buf endless = header(0, "mdat");
        s.write(endless.data(), endless.size());
        check(s.broken(), "a box running to the end of the file ends the cutting: a live stream has no end");
        s.write(good.data(), good.size());
        check(!s.init(), "and nothing after it is read");
    }
    {
        LiveSegments s(zero);
        const Buf small = header(7, "moov");
        s.write(small.data(), small.size());
        check(s.broken(), "a box smaller than its own header");
    }
    {
        LiveSegments s(zero);
        Buf small = header(1, "moov");
        u64(small, 15);
        s.write(small.data(), small.size());
        check(s.broken(), "a 64-bit size smaller than its own header");
    }
    {
        LiveSegments s(zero);
        const Buf huge = header(0xFFFFFFFFu, "mdat");
        s.write(huge.data(), huge.size());
        check(s.broken(), "a box of four gigabytes is not waited for");
    }
    {
        LiveSegments s(zero);
        Buf huge = header(1, "mdat");
        u64(huge, UINT64_MAX);
        s.write(huge.data(), huge.size());
        check(s.broken(), "nor one of 2^64 bytes");
    }
    {
        LiveSegments s(zero);
        const Buf atLimit = header(static_cast<uint32_t>(LiveSegments::kMaxBoxBytes), "mdat");
        s.write(atLimit.data(), atLimit.size());
        check(!s.broken(), "a box at the limit is waited for");
        const Buf over = header(static_cast<uint32_t>(LiveSegments::kMaxBoxBytes) + 1, "mdat");
        LiveSegments t(zero);
        t.write(over.data(), over.size());
        check(t.broken(), "one byte over is not");
    }
    {
        LiveSegments s(zero);
        const Buf partial = {0, 0, 0};
        s.write(partial.data(), partial.size());
        check(!s.broken() && !s.init(), "less than a header is waited for");
        const Buf rest(good.begin() + 3, good.end());
        s.write(rest.data(), rest.size());
        check(held(s.init()) == good, "and cut once the rest arrives");
    }
}

void testMasterPlaylistFollowsTheInit() {
    const Buf good = cat({ftyp(), moov()});
    LiveSegments s(zero);
    s.write(good.data(), good.size());
    check(s.masterPlaylist() == share::masterPlaylist(good.data(), good.size()), "the master playlist is the init segment's");
}

}  // namespace

int main() {
    testNothingBeforeTheHeader();
    testCutsTheSameHoweverTheWritesAreSplit();
    testSegmentsAreSelfContained();
    testPlaylistText();
    testWindow();
    testFeedOrder();
    testWatchdogClock();
    testFragmentThatCannotBeRewritten();
    testLargeSizeBox();
    testBoxesNoStreamHolds();
    testMasterPlaylistFollowsTheInit();
    if (failures == 0) std::printf("live_segments_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
