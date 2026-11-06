// Host-side checks for the AIFF reader (core/audio/ingest/Aiff, #653): the header walk, the 80-bit float
// sample rate, and the sample conversion to s16 that the export's TrackAudio and AudioTranscoder hear.
// Built and run by CI ("Native export DSP tests" in .github/workflows/android.yml) with the system
// compiler; no device.
//
// What is pinned, with exact expected samples: 8-bit (signed, shifted up), 16-bit, and 24/32-bit truncated
// to their top 16 bits with no dither, in big endian (AIFF, AIFC "NONE") and little endian (AIFC "sowt");
// the rate from COMM's 80-bit extended float; unknown chunks (odd sizes padded) and the SSND offset
// skipped; a file fed one byte at a time reads the same as one fed whole; reading stops at COMM's frame
// count; and what is not a readable AIFF (RIFF, a compressed AIFC, 12-bit, no COMM) is refused.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/Aiff.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

using Bytes = std::vector<uint8_t>;

void put(Bytes& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }
void be16(Bytes& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void be32(Bytes& b, uint32_t v) {
    be16(b, v >> 16);
    be16(b, v & 0xFFFF);
}

// The 80-bit IEEE extended float of a whole positive rate, as AIFF writes it.
void ext80(Bytes& b, uint32_t rate) {
    if (rate == 0) {
        b.insert(b.end(), 10, 0);
        return;
    }
    int exp = 31;
    while (!(rate & (1u << exp))) --exp;
    be16(b, static_cast<uint32_t>(16383 + exp));
    const uint64_t mantissa = static_cast<uint64_t>(rate) << (63 - exp);
    be32(b, static_cast<uint32_t>(mantissa >> 32));
    be32(b, static_cast<uint32_t>(mantissa));
}

struct Spec {
    int channels = 2;
    uint32_t frames = 0;
    int bits = 16;
    uint32_t rate = 44100;
    const char* kind = "AIFF";
    const char* compression = nullptr;   // AIFC only: "NONE", "sowt", anything else
    bool junkBefore = false;             // an odd-sized unknown chunk before COMM
    uint32_t ssndOffset = 0;
    bool commFirst = true;
};

Bytes aiff(const Spec& s, const Bytes& data) {
    Bytes comm;
    put(comm, "COMM");
    const bool aifc = s.compression != nullptr;
    be32(comm, aifc ? 18 + 4 + 2 : 18);   // + a pascal string "\0\0" after the compression id
    be16(comm, static_cast<uint32_t>(s.channels));
    be32(comm, s.frames);
    be16(comm, static_cast<uint32_t>(s.bits));
    ext80(comm, s.rate);
    if (aifc) {
        put(comm, s.compression);
        be16(comm, 0);
    }
    Bytes ssnd;
    put(ssnd, "SSND");
    be32(ssnd, static_cast<uint32_t>(8 + s.ssndOffset + data.size()));
    be32(ssnd, s.ssndOffset);
    be32(ssnd, 0);
    ssnd.insert(ssnd.end(), s.ssndOffset, 0xEE);
    ssnd.insert(ssnd.end(), data.begin(), data.end());

    Bytes body;
    put(body, s.kind);
    if (s.junkBefore) {
        put(body, "ANNO");
        be32(body, 5);
        body.insert(body.end(), {'h', 'e', 'l', 'l', 'o', 0});   // 5 bytes + the pad byte
    }
    if (s.commFirst) body.insert(body.end(), comm.begin(), comm.end());
    body.insert(body.end(), ssnd.begin(), ssnd.end());
    if (!s.commFirst) body.insert(body.end(), comm.begin(), comm.end());
    Bytes file;
    put(file, "FORM");
    be32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

struct Read {
    int state = 0;
    int channels = 0;
    int rate = 0;
    std::vector<int16_t> samples;
};

// Feeds `file` in `chunk`-byte writes and reads `capacity` samples at a time, the way AiffPcm does: read
// until the reader wants more bytes, then write the next chunk; at the end of the bytes, read what is left.
Read readAll(const Bytes& file, size_t chunk, int capacity = 4096) {
    Read r;
    geode_aiff* a = geode_aiff_create();
    std::vector<int16_t> out(static_cast<size_t>(capacity));
    size_t at = 0;
    while (true) {
        if (r.state == 1) {
            const int n = geode_aiff_read_s16(a, out.data(), capacity);
            if (n > 0) {
                r.samples.insert(r.samples.end(), out.begin(), out.begin() + n);
                continue;
            }
            if (n < 0) break;
        }
        if (at >= file.size()) break;
        const size_t n = std::min(chunk, file.size() - at);
        r.state = geode_aiff_write(a, file.data() + at, n);
        at += n;
        if (r.state < 0) break;
    }
    geode_aiff_format fmt{};
    geode_aiff_get_format(a, &fmt);
    r.channels = fmt.channels;
    r.rate = fmt.sample_rate;
    geode_aiff_destroy(a);
    return r;
}

bool same(const std::vector<int16_t>& got, std::initializer_list<int16_t> want) {
    return got == std::vector<int16_t>(want);
}

void extendedRate() {
    using geode::audio::ingest::aiffExtendedRate;
    // 44100 Hz: exponent 0x400E, mantissa 0xAC44 << 48.
    const uint8_t r44100[10] = {0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0};
    check(aiffExtendedRate(r44100) == 44100, "44.1 kHz from its 80-bit float");
    const uint8_t r48000[10] = {0x40, 0x0E, 0xBB, 0x80, 0, 0, 0, 0, 0, 0};
    check(aiffExtendedRate(r48000) == 48000, "48 kHz from its 80-bit float");
    const uint8_t r8000[10] = {0x40, 0x0B, 0xFA, 0x00, 0, 0, 0, 0, 0, 0};
    check(aiffExtendedRate(r8000) == 8000, "8 kHz from its 80-bit float");
    // The fraction is dropped (22050.5 Hz reads 22050); the sign bit is ignored.
    const uint8_t r22050half[10] = {0xC0, 0x0D, 0xAC, 0x45, 0, 0, 0, 0, 0, 0};
    check(aiffExtendedRate(r22050half) == 22050, "a fractional rate truncates, the sign is ignored");
    const uint8_t zero[10] = {};
    check(aiffExtendedRate(zero) == 0, "zero reads 0");
    const uint8_t tooBig[10] = {0x40, 0x7F, 0x80, 0, 0, 0, 0, 0, 0, 0};
    check(aiffExtendedRate(tooBig) == 0, "an exponent past 63 bits reads 0");
    const uint8_t tooSmall[10] = {0x3F, 0x00, 0x80, 0, 0, 0, 0, 0, 0, 0};
    check(aiffExtendedRate(tooSmall) == 0, "a rate below 1 reads 0");
    for (uint32_t rate : {11025u, 32000u, 88200u, 96000u, 176400u, 192000u}) {
        Bytes b;
        ext80(b, rate);
        check(aiffExtendedRate(b.data()) == static_cast<int>(rate), "a common rate round-trips");
    }
}

void bitDepths() {
    // 8-bit is signed in AIFF: each byte becomes the top byte of the s16.
    {
        Spec s;
        s.channels = 1;
        s.frames = 4;
        s.bits = 8;
        const Read r = readAll(aiff(s, {0x00, 0x7F, 0x80, 0xFF}), 1 << 16);
        check(r.state == 1 && r.channels == 1 && r.rate == 44100, "8-bit mono header");
        check(same(r.samples, {0, 0x7F00, -32768, -256}), "8-bit samples shift up a byte");
    }
    // 16-bit big endian, as written.
    {
        Spec s;
        s.frames = 2;
        const Read r = readAll(aiff(s, {0x12, 0x34, 0x80, 0x00, 0xFF, 0xFF, 0x7F, 0xFF}), 1 << 16);
        check(same(r.samples, {0x1234, -32768, -1, 32767}), "16-bit big endian");
    }
    // 24-bit big endian: the top two bytes, the low byte dropped (truncated, no dither).
    {
        Spec s;
        s.channels = 1;
        s.frames = 3;
        s.bits = 24;
        const Read r = readAll(aiff(s, {0x12, 0x34, 0x56, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x01}), 1 << 16);
        check(same(r.samples, {0x1234, -1, -32768}), "24-bit big endian truncates to the top 16 bits");
    }
    // 32-bit big endian: the top two bytes.
    {
        Spec s;
        s.channels = 1;
        s.frames = 2;
        s.bits = 32;
        const Read r = readAll(aiff(s, {0x7F, 0xFE, 0xAB, 0xCD, 0xC0, 0x01, 0xFF, 0xFF}), 1 << 16);
        check(same(r.samples, {0x7FFE, static_cast<int16_t>(0xC001)}), "32-bit big endian truncates to the top 16 bits");
    }
}

void endianness() {
    // AIFC "NONE" is big endian like AIFF.
    {
        Spec s;
        s.kind = "AIFC";
        s.compression = "NONE";
        s.channels = 1;
        s.frames = 2;
        const Read r = readAll(aiff(s, {0x12, 0x34, 0xFE, 0xDC}), 1 << 16);
        check(same(r.samples, {0x1234, static_cast<int16_t>(0xFEDC)}), "AIFC NONE reads big endian");
    }
    // AIFC "sowt" is little endian at every depth.
    {
        Spec s;
        s.kind = "AIFC";
        s.compression = "sowt";
        s.channels = 1;
        s.frames = 2;
        const Read r = readAll(aiff(s, {0x34, 0x12, 0xDC, 0xFE}), 1 << 16);
        check(same(r.samples, {0x1234, static_cast<int16_t>(0xFEDC)}), "sowt 16-bit reads little endian");
    }
    {
        Spec s;
        s.kind = "AIFC";
        s.compression = "sowt";
        s.channels = 1;
        s.frames = 2;
        s.bits = 24;
        const Read r = readAll(aiff(s, {0x56, 0x34, 0x12, 0x01, 0x00, 0x80}), 1 << 16);
        check(same(r.samples, {0x1234, -32768}), "sowt 24-bit keeps the top two bytes");
    }
    {
        Spec s;
        s.kind = "AIFC";
        s.compression = "sowt";
        s.channels = 1;
        s.frames = 2;
        s.bits = 32;
        const Read r = readAll(aiff(s, {0xCD, 0xAB, 0xFE, 0x7F, 0xFF, 0xFF, 0x01, 0xC0}), 1 << 16);
        check(same(r.samples, {0x7FFE, static_cast<int16_t>(0xC001)}), "sowt 32-bit keeps the top two bytes");
    }
    {
        Spec s;
        s.kind = "AIFC";
        s.compression = "sowt";
        s.channels = 1;
        s.frames = 2;
        s.bits = 8;
        const Read r = readAll(aiff(s, {0x01, 0xFF}), 1 << 16);
        check(same(r.samples, {0x0100, -256}), "sowt 8-bit is one byte either way");
    }
}

void layout() {
    // An odd-sized chunk before COMM (padded), an SSND offset, 48 kHz stereo: the same samples whether the
    // file comes whole or a byte at a time, and whatever the read size.
    Spec s;
    s.rate = 48000;
    s.junkBefore = true;
    s.ssndOffset = 3;
    Bytes data;
    std::vector<int16_t> want;
    for (int i = 0; i < 1000; ++i) {
        const auto v = static_cast<int16_t>(i * 37 - 18000);
        be16(data, static_cast<uint16_t>(v));
        want.push_back(v);
    }
    s.frames = 500;
    const Bytes file = aiff(s, data);
    const Read whole = readAll(file, file.size());
    check(whole.state == 1 && whole.rate == 48000 && whole.channels == 2, "48 kHz stereo header past a chunk");
    check(whole.samples == want, "junk chunk and SSND offset skipped");
    const Read bytewise = readAll(file, 1, 6);
    check(bytewise.samples == want, "a byte at a time, six samples a read, reads the same");
    const Read odd = readAll(file, 7, 5);   // 5 rounds down to whole stereo frames
    check(odd.samples == want, "odd chunks and an odd read size read the same");

    // Reading stops at COMM's frame count, whatever follows.
    Spec shorter = s;
    shorter.frames = 3;
    const Read cut = readAll(aiff(shorter, data), 1 << 16);
    check(cut.samples.size() == 6 && std::equal(cut.samples.begin(), cut.samples.end(), want.begin()),
          "reading stops at the frame count");

    // Progress counts frames read against the total.
    geode_aiff* a = geode_aiff_create();
    check(geode_aiff_write(a, file.data(), file.size()) == 1, "ready after the whole file");
    std::vector<int16_t> out(500);
    check(geode_aiff_read_s16(a, out.data(), 500) == 500, "a read fills whole frames");
    check(geode_aiff_progress(a) == 0.5f, "half read is progress 0.5");
    check(geode_aiff_read_s16(a, out.data(), 500) == 500, "the second half");
    check(geode_aiff_progress(a) == 1.0f, "all read is progress 1");
    check(geode_aiff_read_s16(a, out.data(), 500) == -1, "past the frame count reads -1");
    geode_aiff_destroy(a);

    // Before any bytes nothing is ready. A capacity below one frame ends the read (-1, as the Kotlin
    // reader did), so a caller that loops on 0 never buffers the whole file.
    geode_aiff* b = geode_aiff_create();
    check(geode_aiff_read_s16(b, out.data(), 500) == 0, "nothing to read before the header");
    geode_aiff_write(b, file.data(), file.size());
    check(geode_aiff_read_s16(b, out.data(), 1) == -1, "a capacity under one frame ends the read");
    geode_aiff_destroy(b);

    // SSND's offset is signed, and one at or below zero is ignored (the Kotlin rule): a high bit set never
    // skips 4 GB into the file.
    Spec plain;
    plain.frames = 2;
    Bytes negative = aiff(plain, {0, 1, 0, 2, 0, 3, 0, 4});
    const auto ssnd = std::search(negative.begin(), negative.end(), "SSND", "SSND" + 4);
    std::fill(ssnd + 8, ssnd + 12, 0xFF);   // offset -1
    const Read neg = readAll(negative, 1 << 16);
    check(neg.state == 1 && same(neg.samples, {1, 2, 3, 4}), "a negative SSND offset is ignored");
}

void refused() {
    Spec s;
    s.frames = 1;
    const Bytes good = aiff(s, {0, 1, 0, 2});
    Bytes riff = good;
    std::memcpy(riff.data(), "RIFF", 4);
    check(readAll(riff, 1 << 16).state == -1, "RIFF is not AIFF");
    Bytes wave = good;
    std::memcpy(wave.data() + 8, "WAVE", 4);
    check(readAll(wave, 1 << 16).state == -1, "FORM of another kind is refused");

    Spec comp = s;
    comp.kind = "AIFC";
    comp.compression = "ulaw";
    check(readAll(aiff(comp, {0, 1}), 1 << 16).state == -1, "a compressed AIFC is refused");

    Spec twelve = s;
    twelve.bits = 12;
    check(readAll(aiff(twelve, {0, 1, 0, 2}), 1 << 16).state == -1, "12-bit is refused");

    Spec noRate = s;
    noRate.rate = 0;
    Bytes zeroRate = aiff(noRate, {0, 1, 0, 2});
    check(readAll(zeroRate, 1 << 16).state == -1, "rate 0 is refused");

    Spec late = s;
    late.commFirst = false;
    check(readAll(aiff(late, {0, 1, 0, 2}), 1 << 16).state == -1, "SSND before COMM is refused");

    Bytes truncated(good.begin(), good.begin() + 20);
    check(readAll(truncated, 1 << 16).state == 0, "a header cut short is still waiting for bytes");
}

}  // namespace

int main() {
    extendedRate();
    bitDepths();
    endianness();
    layout();
    refused();
    if (failures == 0) std::printf("aiff_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
