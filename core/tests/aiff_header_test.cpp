// Host-side checks for the AIFF header scan (core/audio/ingest/AiffReader: AiffHeaderScan), the one parse
// behind both the export's reader and the player's extractor, and its C ABI: what COMM and SSND say for every
// layout, the chunk walk with pads, extra chunks and SSND's offset, the 80-bit rate, a header cut at every
// length, corrupt and hostile headers (sizes of four gigabytes, wrong tags, no channels), and the seek map's
// times and offsets without overflow. The files are built here.
// Built and run by CI (.github/workflows/native-host-tests.yml, beside track_feed_test); no device.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/AiffReader.hpp"

using namespace geode::audio::ingest;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

using Bytes = std::vector<uint8_t>;
using Status = AiffHeaderScan::Status;

// ── Building files ───────────────────────────────────────────────────────────────────────────────────────

void put16(Bytes& b, int v) {
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void put32(Bytes& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

void tag(Bytes& b, const char* t) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(t[i]));
}

// The ten bytes of mantissa * 2^exp2 as an 80-bit extended number.
Bytes ext(uint64_t mantissa, int exp2, bool negative = false) {
    int exponent = 16383 + 63 + exp2;
    if (mantissa != 0) {
        while (!(mantissa >> 63)) {
            mantissa <<= 1;
            --exponent;
        }
    }
    Bytes b;
    put16(b, exponent | (negative ? 0x8000 : 0));
    put32(b, static_cast<uint32_t>(mantissa >> 32));
    put32(b, static_cast<uint32_t>(mantissa & 0xFFFFFFFFu));
    return b;
}

Bytes comm(int channels, uint32_t frames, int bits, const Bytes& rate, const char* compression = nullptr) {
    Bytes b;
    put16(b, channels);
    put32(b, frames);
    put16(b, bits);
    b.insert(b.end(), rate.begin(), rate.end());
    if (compression) {
        tag(b, compression);
        b.push_back(0);  // an empty name, padded
        b.push_back(0);
    }
    return b;
}

void head(Bytes& file, const char* id, uint32_t size) {
    tag(file, id);
    put32(file, size);
}

// A chunk with its pad byte when the body is odd.
void chunk(Bytes& file, const char* id, const Bytes& body) {
    head(file, id, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    if (body.size() & 1) file.push_back(0);
}

Bytes form(const char* kind) {
    Bytes b;
    tag(b, "FORM");
    put32(b, 0);  // the scan never trusts FORM's size
    tag(b, kind);
    return b;
}

Bytes sound(uint32_t offset, const Bytes& data) {
    Bytes b;
    put32(b, offset);
    put32(b, 0);
    b.insert(b.end(), static_cast<size_t>(offset), uint8_t{0xEE});
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

Bytes file(const char* kind, const Bytes& commBody, uint32_t offset, const Bytes& data) {
    Bytes f = form(kind);
    chunk(f, "COMM", commBody);
    chunk(f, "SSND", sound(offset, data));
    return f;
}

const Bytes kRate = ext(44100, 0);

// ── Feeding the scan ─────────────────────────────────────────────────────────────────────────────────────

enum Outcome { kParsed, kRefused, kCutShort };

// Feeds a scan from `bytes` as a reader would, refusing to pass or read beyond the end.
Outcome drive(const Bytes& bytes, AiffHeader* out = nullptr) {
    AiffHeaderScan scan;
    size_t at = 0;
    for (int steps = 0; steps < 10'000; ++steps) {
        const int64_t gap = scan.skip();
        const int want = scan.read();
        check(gap >= 0 && gap <= AiffHeaderScan::kMaxSkip, "a gap within the most it asks to pass");
        check(want >= 0 && want <= AiffHeaderScan::kMaxRead, "a read within the most it asks for");
        check(gap > 0 || want > 0, "every step moves on through the file");
        if (static_cast<uint64_t>(gap) > bytes.size() - at) return kCutShort;
        at += static_cast<size_t>(gap);
        if (static_cast<size_t>(want) > bytes.size() - at) return kCutShort;
        const Status status = scan.step(want > 0 ? bytes.data() + at : nullptr, want);
        at += static_cast<size_t>(want);
        if (status == AiffHeaderScan::kInvalid) {
            check(!scan.done() && scan.skip() == 0 && scan.read() == 0, "a refused scan asks for nothing more");
            check(scan.step(bytes.data(), 0) == AiffHeaderScan::kInvalid, "and stays refused");
            return kRefused;
        }
        if (status == AiffHeaderScan::kDone) {
            check(scan.done() && scan.skip() == 0 && scan.read() == 0, "a finished scan asks for nothing more");
            check(scan.step(nullptr, 0) == AiffHeaderScan::kDone, "and stays finished");
            check(scan.header().dataStart == static_cast<int64_t>(at), "the feeder stands on the first sample");
            if (out) *out = scan.header();
            return kParsed;
        }
    }
    check(false, "the walk ends");
    return kRefused;
}

std::unique_ptr<AiffReader> openBytes(const Bytes& bytes) {
    return AiffReader::open(std::make_unique<MemorySource>(bytes));
}

bool refused(const Bytes& bytes) { return drive(bytes) == kRefused && !openBytes(bytes); }

// A scan over a file that is not there: the test hands it each read and counts what it passes over.
struct Walk {
    AiffHeaderScan scan;
    int64_t at = 0;
    int gaps = 0;  // steps that only passed bytes over

    // Passes what the scan asks, then hands it `bytes`; with none, runs until the scan wants bytes or ends.
    Status feed(const Bytes& bytes) {
        for (;;) {
            at += scan.skip();
            const int want = scan.read();
            if (want > 0) {
                if (static_cast<int>(bytes.size()) != want) {
                    check(false, "the walk was handed the bytes the scan asks for");
                    return AiffHeaderScan::kInvalid;
                }
                at += want;
                return scan.step(bytes.data(), want);
            }
            ++gaps;
            const Status status = scan.step(nullptr, 0);
            if (status != AiffHeaderScan::kMore) return status;
        }
    }
};

// ── The checks ───────────────────────────────────────────────────────────────────────────────────────────

void layouts() {
    const Bytes data(12, 0x11);
    AiffHeader h;
    check(drive(file("AIFF", comm(2, 3, 16, kRate), 0, data), &h) == kParsed, "a plain AIFF parses");
    check(h.channels == 2 && h.sampleRate == 44100 && h.bits == 16 && !h.littleEndian && h.frames == 3, "its COMM");
    check(h.dataStart == 54 && h.dataBytes == 12, "its SSND");
    check(h.pcm() == AiffHeader::kS16Be && h.bytesPerFrame() == 4, "16-bit big-endian, four bytes a frame");

    check(drive(file("AIFC", comm(2, 3, 16, kRate, "sowt"), 0, data), &h) == kParsed, "AIFF-C sowt parses");
    check(h.littleEndian && h.pcm() == AiffHeader::kS16Le && h.dataStart == 60, "sowt is 16-bit little-endian");
    check(drive(file("AIFC", comm(1, 4, 24, kRate, "NONE"), 0, data), &h) == kParsed, "AIFF-C NONE parses");
    check(!h.littleEndian && h.pcm() == AiffHeader::kS24Be && h.bytesPerFrame() == 3, "24-bit big-endian");
    check(drive(file("AIFF", comm(6, 0, 32, kRate), 0, data), &h) == kParsed && h.pcm() == AiffHeader::kS32Be &&
              h.bytesPerFrame() == 24,
          "32-bit big-endian, six channels");

    // Layouts the scan reads and the player's sink has no encoding for.
    check(drive(file("AIFF", comm(2, 0, 8, kRate), 0, data), &h) == kParsed && h.pcm() == AiffHeader::kNotPlayed &&
              h.bytesPerFrame() == 2,
          "8-bit is read, not played");
    check(drive(file("AIFC", comm(2, 0, 24, kRate, "sowt"), 0, data), &h) == kParsed &&
              h.pcm() == AiffHeader::kNotPlayed,
          "24-bit little-endian is not played");
    check(drive(file("AIFF", comm(2, 0, 12, kRate), 0, data), &h) == kParsed && h.pcm() == AiffHeader::kNotPlayed &&
              h.bytesPerFrame() == 0,
          "a width that is not whole bytes has no frame size");
    check(!openBytes(file("AIFF", comm(2, 0, 12, kRate), 0, data)), "and the reader refuses it");

    // The reader and the scan say the same of the same file.
    auto r = openBytes(file("AIFC", comm(2, 3, 16, ext(88201, -1), "sowt"), 0, data));
    check(r && r->channels() == 2 && r->sampleRate() == 44101 && r->bitsPerSample() == 16 && r->littleEndian() &&
              r->totalFrames() == 3,
          "the reader opens through the scan");
    int16_t out[8] = {};
    check(r && r->readS16(out, 8) == 6 && out[0] == 0x1111, "and stands on the first sample");
}

void chunkWalk() {
    const Bytes data = {1, 2, 3, 4, 5, 6, 7, 8};
    Bytes odd = comm(2, 2, 16, ext(48000, 0));
    odd.push_back(0x55);  // a 19-byte COMM, padded to 20
    Bytes f = form("AIFF");
    chunk(f, "FVER", {0xA2, 0x80, 0x51, 0x40});
    chunk(f, "NAME", {'a', 'b', 'c'});
    chunk(f, "COMM", odd);
    chunk(f, "ANNO", {1, 2, 3, 4, 5});
    chunk(f, "SSND", sound(6, data));
    AiffHeader h;
    check(drive(f, &h) == kParsed, "other chunks, odd ones padded, and SSND's offset are passed over");
    check(h.sampleRate == 48000 && h.dataStart == 100 && h.dataBytes == 8, "to the first sample");
    check(f[100] == 1, "which is where the file has it");

    // A header cut anywhere before the first sample never parses, and the scan never reads past the cut.
    bool cut = true;
    for (size_t length = 0; length < 100; ++length) {
        cut = cut && drive(Bytes(f.begin(), f.begin() + static_cast<std::ptrdiff_t>(length))) == kCutShort;
    }
    check(cut, "cut at every length, the feeder runs out");
    check(drive(Bytes(f.begin(), f.begin() + 100)) == kParsed, "the whole header and no samples still parses");
    // The reader passes a gap as far as the file goes, so it notices a cut at the next read: before SSND's words end.
    bool refusedCut = true;
    for (size_t length = 0; length < 94; ++length) {
        refusedCut = refusedCut && !openBytes(Bytes(f.begin(), f.begin() + static_cast<std::ptrdiff_t>(length)));
    }
    check(refusedCut, "cut at every length, the reader refuses");
    auto bare = openBytes(Bytes(f.begin(), f.begin() + 100));
    int16_t out[4] = {};
    check(bare && bare->readS16(out, 4) == -1, "a header with no samples opens and ends at once");

    Bytes twice = form("AIFF");
    chunk(twice, "COMM", comm(1, 0, 16, ext(8000, 0)));
    chunk(twice, "COMM", comm(2, 0, 16, ext(48000, 0)));
    chunk(twice, "SSND", sound(0, data));
    check(drive(twice, &h) == kParsed && h.channels == 2 && h.sampleRate == 48000, "a second COMM replaces the first");

    Bytes shortType = comm(2, 0, 16, kRate);
    shortType.push_back('s');
    shortType.push_back('o');
    check(drive(file("AIFC", shortType, 0, data), &h) == kParsed && !h.littleEndian,
          "an AIFF-C COMM too short for a compression type is uncompressed");
    Bytes exact = comm(2, 0, 16, kRate);
    tag(exact, "sowt");
    check(drive(file("AIFC", exact, 0, data), &h) == kParsed && h.littleEndian, "a COMM that ends on the type");
    check(drive(file("AIFF", comm(2, 0, 16, kRate, "alaw"), 0, data), &h) == kParsed && !h.littleEndian,
          "a plain AIFF's longer COMM is not read for a type");
}

void corrupt() {
    const Bytes data(8, 0);
    const Bytes good = comm(2, 0, 16, kRate);

    check(refused({'R', 'I', 'F', 'F', 0, 0, 0, 4, 'W', 'A', 'V', 'E'}), "a WAV");
    check(refused(file("AIFX", good, 0, data)), "a FORM of another kind");
    Bytes lower = file("AIFF", good, 0, data);
    lower[0] = 'f';
    check(refused(lower), "a tag in the wrong case");

    Bytes shortComm = form("AIFF");
    head(shortComm, "COMM", 17);
    shortComm.insert(shortComm.end(), 40, uint8_t{0});
    check(refused(shortComm), "a COMM under 18 bytes");
    Bytes emptyComm = form("AIFF");
    head(emptyComm, "COMM", 0);
    chunk(emptyComm, "SSND", sound(0, data));
    check(refused(emptyComm), "an empty COMM");

    check(refused(file("AIFF", comm(0, 0, 16, kRate), 0, data)), "no channels");
    check(refused(file("AIFF", comm(0xFFFF, 0, 16, kRate), 0, data)), "less than no channels");
    check(refused(file("AIFF", comm(2, 0, 0, kRate), 0, data)), "no sample size");
    check(refused(file("AIFF", comm(2, 0, 0x8010, kRate), 0, data)), "a negative sample size");
    check(refused(file("AIFF", comm(2, 0, 16, ext(0, 0)), 0, data)), "no rate");
    check(refused(file("AIFF", comm(2, 0, 16, ext(1, 40)), 0, data)), "a rate past what an int holds");
    check(refused(file("AIFC", comm(2, 0, 16, kRate, "alaw"), 0, data)), "a compressed AIFF-C");
    check(refused(file("AIFC", comm(2, 0, 16, kRate, "SOWT"), 0, data)), "a compression type in the wrong case");

    Bytes soundFirst = form("AIFF");
    chunk(soundFirst, "SSND", sound(0, data));
    chunk(soundFirst, "COMM", good);
    check(refused(soundFirst), "SSND before COMM");

    check(!openBytes({}) && drive({}) == kCutShort, "no bytes at all");
    check(!openBytes(form("AIFF")) && drive(form("AIFF")) == kCutShort, "FORM and nothing else");
    check(!AiffReader::open(nullptr), "no source");

    // A chunk that claims four gigabytes in a file of a few bytes: the reader ends, at once, with nothing.
    Bytes huge = form("AIFF");
    head(huge, "JUNK", 0xFFFFFFFFu);
    huge.insert(huge.end(), 40, uint8_t{0});
    check(!openBytes(huge) && drive(huge) == kCutShort, "a chunk longer than the file");

    // A step that is not what the scan asked for ends it.
    const Bytes start = form("AIFF");
    AiffHeaderScan wrongCount;
    check(wrongCount.step(start.data(), 11) == AiffHeaderScan::kInvalid, "fewer bytes than asked");
    AiffHeaderScan tooMany;
    check(tooMany.step(start.data(), 13) == AiffHeaderScan::kInvalid, "more bytes than asked");
    AiffHeaderScan noBytes;
    check(noBytes.step(nullptr, 12) == AiffHeaderScan::kInvalid, "no bytes where some were asked");
    AiffHeaderScan negative;
    check(negative.step(start.data(), -1) == AiffHeaderScan::kInvalid, "a negative count");
}

void hostileSizes() {
    const int64_t fourGb = int64_t{1} << 32;

    // The largest chunk the format can name, odd, so padded to four gigabytes: passed in pieces.
    Walk w;
    Bytes junk;
    head(junk, "JUNK", 0xFFFFFFFFu);
    Bytes commHead;
    head(commHead, "COMM", 18);
    check(w.feed(form("AIFF")) == AiffHeaderScan::kMore && w.feed(junk) == AiffHeaderScan::kMore, "a huge chunk's head");
    check(w.feed(commHead) == AiffHeaderScan::kMore, "the chunk after it");
    check(w.gaps == 3 && w.at == 12 + 8 + fourGb + 8, "four gigabytes passed in four pieces");
    check(w.feed(comm(2, 0, 16, kRate)) == AiffHeaderScan::kMore, "COMM");

    // SSND with the largest size and the largest offset: the first sample is past both, no sample bytes left.
    Bytes ssndHead;
    head(ssndHead, "SSND", 0xFFFFFFFFu);
    Bytes words;
    put32(words, 0xFFFFFFFFu);
    put32(words, 0);
    check(w.feed(ssndHead) == AiffHeaderScan::kMore && w.feed(words) == AiffHeaderScan::kMore, "SSND and its offset");
    check(w.feed({}) == AiffHeaderScan::kDone && w.gaps == 7, "the offset passed in four more pieces");
    const int64_t first = 12 + 8 + fourGb + 8 + 18 + 8 + 8 + int64_t{0xFFFFFFFF};
    check(w.at == first && w.scan.header().dataStart == first, "the first sample lies past eight gigabytes");
    check(w.scan.header().dataBytes == 0, "an offset past SSND's size leaves no samples");

    Walk big;
    big.feed(form("AIFF"));
    big.feed(commHead);
    big.feed(comm(2, 0, 16, kRate));
    big.feed(ssndHead);
    Bytes none;
    put32(none, 0);
    put32(none, 0);
    check(big.feed(none) == AiffHeaderScan::kDone && big.scan.header().dataBytes == int64_t{0xFFFFFFF7},
          "SSND's size is unsigned");

    Walk tiny;
    tiny.feed(form("AIFF"));
    tiny.feed(commHead);
    tiny.feed(comm(2, 0, 16, kRate));
    Bytes smallHead;
    head(smallHead, "SSND", 4);
    tiny.feed(smallHead);
    check(tiny.feed(none) == AiffHeaderScan::kDone && tiny.scan.header().dataBytes == 0,
          "an SSND smaller than its own words has no samples");

    // A COMM of the largest size: its 18 bytes are read and the rest passed over.
    Walk wide;
    wide.feed(form("AIFF"));
    Bytes wideHead;
    head(wideHead, "COMM", 0xFFFFFFFEu);
    check(wide.feed(wideHead) == AiffHeaderScan::kMore && wide.feed(comm(2, 0, 16, kRate)) == AiffHeaderScan::kMore,
          "a COMM that claims four gigabytes");
    check(wide.feed(smallHead) == AiffHeaderScan::kMore && wide.at == 12 + 8 + int64_t{0xFFFFFFFE} + 8,
          "is passed whole");
}

int rateOf(const Bytes& ten) { return AiffHeaderScan::extendedRate(ten.data()); }

void rates() {
    for (uint32_t rate : {1u, 8000u, 11025u, 22050u, 44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 384000u}) {
        check(rateOf(ext(rate, 0)) == static_cast<int>(rate), "a whole rate is itself");
    }
    check(rateOf(ext(88201, -1)) == 44101, "44100.5 rounds up");
    check(rateOf(ext(88199, -1)) == 44100, "44099.5 rounds up");
    check(rateOf(ext(176401, -2)) == 44100, "44100.25 rounds down");
    check(rateOf(ext(176403, -2)) == 44101, "44100.75 rounds up");
    check(rateOf(ext(1, -1)) == 1 && rateOf(ext(1, -2)) == 0, "a half rounds to one, a quarter to none");
    check(rateOf(ext(1, -200)) == 0, "a rate far under one is none");
    check(rateOf(ext(44100, 0, true)) == 44100, "the sign bit is not read");
    check(rateOf(ext(0x7FFFFFFFu, 0)) == std::numeric_limits<int32_t>::max(), "the largest rate an int holds");
    check(rateOf(ext(0x80000000u, 0)) == 0 && rateOf(ext(0xFFFFFFFFu, -1)) == 0, "one past it is none");
    check(rateOf(ext(1, 63)) == 0 && rateOf(ext(1, 64)) == 0 && rateOf(ext(1, 5000)) == 0, "a huge rate is none");
    check(rateOf(ext(0, 0)) == 0 && rateOf(ext(0, 20)) == 0, "no mantissa is none");
    check(rateOf({0x7F, 0xFF, 0x80, 0, 0, 0, 0, 0, 0, 0}) == 0, "infinity is none");
    check(rateOf({0x7F, 0xFF, 0xC0, 0, 0, 0, 0, 0, 0, 1}) == 0, "not-a-number is none");
    check(rateOf({0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}) == 0, "a denormal is none");
}

void seekMap() {
    const int64_t most = std::numeric_limits<int64_t>::max();
    check(aiffTimeUs(0, 4, 44100) == 0 && aiffTimeUs(-8, 4, 44100) == 0, "the first frame, and before it");
    check(aiffTimeUs(4 * 44100, 4, 44100) == 1'000'000, "a second of frames");
    check(aiffTimeUs(12, 4, 44100) == 68 && aiffTimeUs(15, 4, 44100) == 68, "a byte inside a frame is that frame");
    check(aiffTimeUs(100, 0, 44100) == 0 && aiffTimeUs(100, 4, 0) == 0 && aiffTimeUs(100, -4, -1) == 0, "a bad layout");
    const int64_t frames = 1'000'000'000'000;
    const auto exact = static_cast<int64_t>(static_cast<__int128>(frames) * 1'000'000 / 44100);
    check(aiffTimeUs(frames * 4, 4, 44100) == exact, "a file of a trillion frames, to the microsecond");
    check(aiffTimeUs(most, 1, 1) == most && aiffTimeUs(most, 1, 2) > 0, "the longest file does not overflow");

    const int64_t tenSeconds = 4 * 441'000;
    check(aiffSeekOffset(0, 4, 44100, tenSeconds) == 0 && aiffSeekOffset(-5, 4, 44100, tenSeconds) == 0, "the start");
    check(aiffSeekOffset(1'000'000, 4, 44100, tenSeconds) == 4 * 44100, "a second in");
    check(aiffSeekOffset(68, 4, 44100, tenSeconds) == 8 && aiffSeekOffset(69, 4, 44100, tenSeconds) == 12,
          "the frame playing at a time");
    check(aiffSeekOffset(10'500'000, 4, 44100, tenSeconds) == tenSeconds, "past the end is the end");
    check(aiffSeekOffset(most, 4, 44100, tenSeconds) == tenSeconds, "the farthest seek is the end");
    check(aiffSeekOffset(most, 4, 44100, 43) == 40, "the end is the last whole frame");
    check(aiffSeekOffset(most, 1, std::numeric_limits<int32_t>::max(), most) > 0, "the longest file and fastest rate");
    check(aiffSeekOffset(500, 0, 44100, 100) == 0 && aiffSeekOffset(500, 4, 0, 100) == 0 &&
              aiffSeekOffset(500, 4, 44100, 0) == 0 && aiffSeekOffset(500, 4, 44100, -9) == 0,
          "a bad layout or no samples");

    // A seek lands on a frame boundary at or before the time asked, inside the samples.
    bool lands = true;
    for (int64_t t = 0; t < 12'000'000; t += 7'919) {
        const int64_t offset = aiffSeekOffset(t, 6, 48000, 6 * 480'000 + 5);
        lands = lands && offset % 6 == 0 && offset <= 6 * 480'000 && aiffTimeUs(offset, 6, 48000) <= t;
        if (t < 10'000'000) lands = lands && t - aiffTimeUs(offset, 6, 48000) < 22;  // one frame is 20.8 us
    }
    check(lands, "every seek lands on the frame playing then");
}

void abi() {
    const Bytes f = file("AIFC", comm(2, 10, 16, kRate, "sowt"), 0, Bytes(40, 0));
    geode_aiff_scan* s = geode_aiff_scan_create();
    check(s != nullptr, "ABI create");
    int64_t fields[GEODE_AIFF_HEADER_COUNT];
    for (int64_t& v : fields) v = -7;
    check(geode_aiff_scan_header(s, fields, GEODE_AIFF_HEADER_COUNT) == 0 && fields[0] == -7,
          "ABI: no header before the scan is done");

    size_t at = 0;
    int status = GEODE_AIFF_SCAN_MORE;
    while (status == GEODE_AIFF_SCAN_MORE && at + GEODE_AIFF_SCAN_MAX_READ <= f.size()) {
        at += static_cast<size_t>(geode_aiff_scan_skip(s));
        const int want = geode_aiff_scan_read(s);
        status = geode_aiff_scan_step(s, f.data() + at, want);
        at += static_cast<size_t>(want);
    }
    check(status == GEODE_AIFF_SCAN_DONE && at == 60, "ABI: the walk ends on the first sample");
    check(geode_aiff_scan_header(s, fields, GEODE_AIFF_HEADER_COUNT - 1) == 0 && fields[0] == -7,
          "ABI: too small an output is left alone");
    check(geode_aiff_scan_header(s, nullptr, GEODE_AIFF_HEADER_COUNT) == 0, "ABI: no output");
    check(geode_aiff_scan_header(s, fields, GEODE_AIFF_HEADER_COUNT) == 1, "ABI header");
    check(fields[GEODE_AIFF_HEADER_CHANNELS] == 2 && fields[GEODE_AIFF_HEADER_SAMPLE_RATE] == 44100 &&
              fields[GEODE_AIFF_HEADER_PCM] == GEODE_AIFF_PCM_S16_LE && fields[GEODE_AIFF_HEADER_BYTES_PER_FRAME] == 4,
          "ABI: the layout");
    check(fields[GEODE_AIFF_HEADER_FRAMES] == 10 && fields[GEODE_AIFF_HEADER_DATA_START] == 60 &&
              fields[GEODE_AIFF_HEADER_DATA_BYTES] == 40,
          "ABI: the samples");
    geode_aiff_scan_destroy(s);

    geode_aiff_scan* bad = geode_aiff_scan_create();
    const Bytes wav = {'R', 'I', 'F', 'F', 0, 0, 0, 4, 'W', 'A', 'V', 'E'};
    check(geode_aiff_scan_step(bad, wav.data(), 12) == GEODE_AIFF_SCAN_INVALID, "ABI: refused");
    check(geode_aiff_scan_header(bad, fields, GEODE_AIFF_HEADER_COUNT) == 0, "ABI: a refused scan has no header");
    geode_aiff_scan_destroy(bad);

    check(geode_aiff_scan_skip(nullptr) == 0 && geode_aiff_scan_read(nullptr) == 0, "ABI: no handle asks for nothing");
    check(geode_aiff_scan_step(nullptr, f.data(), 12) == GEODE_AIFF_SCAN_INVALID, "ABI: no handle is refused");
    check(geode_aiff_scan_header(nullptr, fields, GEODE_AIFF_HEADER_COUNT) == 0, "ABI: no handle has no header");
    geode_aiff_scan_destroy(nullptr);

    check(geode_aiff_time_us(4 * 44100, 4, 44100) == 1'000'000, "ABI time");
    check(geode_aiff_seek_offset(1'000'000, 4, 44100, 4 * 441'000) == 4 * 44100, "ABI seek offset");
}

}  // namespace

int main() {
    layouts();
    chunkWalk();
    corrupt();
    hostileSizes();
    rates();
    seekMap();
    abi();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("aiff_header_test: all passed\n");
    return 0;
}
