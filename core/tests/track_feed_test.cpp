// Host-side checks for an export's track audio (core/audio/ingest/TrackFeed) and the AIFF reader beside it
// (core/audio/ingest/AiffReader), each pinned to the Kotlin it replaced: render/offscreen/TrackAudio (the 16-bit
// and float conversion, the codec's pre-roll cut, the AIFF lead-in skip, the frame-exact hand-out) and
// dev.geode.audio.AiffPcm (the header walk, the sample rule, progress). Every expected value below is what the
// Kotlin computed for the same input; the AIFF files are built here. Built and run by CI
// (.github/workflows/native-host-tests.yml) with the system compiler; no device.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore -Icore/tests/stub core/tests/track_feed_test.cpp
//     core/audio/ingest/AiffReader.cpp core/audio/ingest/TrackFeed.cpp core/audio/ingest/PcmRing.cpp
//     core/api/geode_ingest_api.cpp -o track_feed_test && ./track_feed_test
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/AiffReader.hpp"
#include "audio/ingest/TrackFeed.hpp"

using namespace geode::audio::ingest;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// ── AIFF building ────────────────────────────────────────────────────────────────────────────────────────

void be16(std::vector<uint8_t>& b, int v) {
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void be32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// The 80-bit extended form of an integer rate, as every encoder writes it.
void extended(std::vector<uint8_t>& b, uint32_t rate) {
    int exponent = 16383 + 31;
    uint32_t m = rate;
    while (!(m & 0x80000000u)) {
        m <<= 1;
        --exponent;
    }
    be16(b, exponent);
    be32(b, m);
    be32(b, 0);
}

struct Aiff {
    int channels = 2;
    uint32_t rate = 44100;
    int bits = 16;
    const char* compression = nullptr;  // null: plain AIFF; "NONE" or "sowt": AIFC
    uint32_t frames = 0;                // the COMM count
    uint32_t ssndOffset = 0;
    bool junkChunk = false;
    bool oddChunk = false;
    std::vector<uint8_t> data;
};

std::vector<uint8_t> build(const Aiff& a) {
    std::vector<uint8_t> body;
    tag(body, a.compression ? "AIFC" : "AIFF");
    if (a.junkChunk) {
        tag(body, "FVER");
        be32(body, 4);
        be32(body, 0xA2805140u);
    }
    if (a.oddChunk) {
        tag(body, "NAME");
        be32(body, 3);
        body.insert(body.end(), {'a', 'b', 'c', 0});  // padded to even
    }
    tag(body, "COMM");
    const uint32_t commSize = a.compression ? 18 + 4 + 2 : 18;  // AIFC: type, then an empty pstring (2 bytes)
    be32(body, commSize);
    be16(body, a.channels);
    be32(body, a.frames);
    be16(body, a.bits);
    extended(body, a.rate);
    if (a.compression) {
        tag(body, a.compression);
        body.push_back(0);
        body.push_back(0);
    }
    tag(body, "SSND");
    be32(body, static_cast<uint32_t>(8 + a.ssndOffset + a.data.size()));
    be32(body, a.ssndOffset);
    be32(body, 0);
    for (uint32_t i = 0; i < a.ssndOffset; ++i) body.push_back(0xEE);
    body.insert(body.end(), a.data.begin(), a.data.end());
    std::vector<uint8_t> file;
    tag(file, "FORM");
    be32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

std::unique_ptr<AiffReader> openBytes(const std::vector<uint8_t>& bytes) {
    return AiffReader::open(std::make_unique<MemorySource>(bytes));
}

// The Kotlin AiffPcm.read sample rule, written out per sample as it was.
int16_t kotlinSample(const uint8_t* raw, int o, int bytesPer, int bits, bool littleEndian) {
    const int hi = littleEndian ? o + bytesPer - 1 : o;
    const int lo = littleEndian ? o + bytesPer - 2 : o + 1;
    switch (bits) {
    case 8: return static_cast<int16_t>(static_cast<int8_t>(raw[o]) << 8);
    case 16:
    case 24:
    case 32: return static_cast<int16_t>(((raw[hi] & 0xFF) << 8) | (raw[lo] & 0xFF));
    default: return 0;
    }
}

void headers() {
    Aiff plain;
    plain.frames = 3;
    plain.data = {0x12, 0x34, 0x80, 0x00, 0x7F, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x40, 0x00};
    auto r = openBytes(build(plain));
    check(r && r->channels() == 2 && r->sampleRate() == 44100 && r->totalFrames() == 3 && !r->littleEndian(),
          "a plain AIFF's COMM");
    for (uint32_t rate : {8000u, 22050u, 44100u, 48000u, 88200u, 96000u, 176400u, 192000u}) {
        Aiff a = plain;
        a.rate = rate;
        auto x = openBytes(build(a));
        check(x && x->sampleRate() == static_cast<int>(rate), "the 80-bit rate " + std::to_string(rate));
    }
    check(r && r->durationUs() == 3 * 1'000'000LL / 44100, "duration from the frame count");

    Aiff sowt = plain;
    sowt.compression = "sowt";
    auto s = openBytes(build(sowt));
    check(s && s->littleEndian(), "AIFC sowt is little-endian");
    Aiff none = plain;
    none.compression = "NONE";
    auto n = openBytes(build(none));
    check(n && !n->littleEndian(), "AIFC NONE is big-endian");
    Aiff alaw = plain;
    alaw.compression = "alaw";
    check(!openBytes(build(alaw)), "a compressed AIFC is refused");

    Aiff chunks = plain;
    chunks.junkChunk = true;
    chunks.oddChunk = true;
    chunks.ssndOffset = 6;
    auto c = openBytes(build(chunks));
    int16_t got[6] = {};
    check(c && c->readS16(got, 6) == 6 && got[0] == 0x1234, "other chunks, an odd one padded, and SSND's offset are skipped");

    for (int bits : {12, 0, 64}) {
        Aiff b = plain;
        b.bits = bits;
        check(!openBytes(build(b)), "a " + std::to_string(bits) + "-bit AIFF is refused");
    }
    Aiff mute = plain;
    mute.channels = 0;
    check(!openBytes(build(mute)), "no channels is refused");

    check(!openBytes({'R', 'I', 'F', 'F', 0, 0, 0, 4, 'W', 'A', 'V', 'E'}), "a WAV is not an AIFF");
    auto cut = build(plain);
    cut.resize(30);
    check(!openBytes(cut), "a header cut short is refused");
    auto noSound = build(plain);
    noSound.resize(12 + 8 + 18);  // FORM, kind, COMM only
    check(!openBytes(noSound), "no SSND is refused");
}

void samples() {
    // Every width and byte order against the Kotlin rule, over a pseudo-random byte stream.
    for (int bits : {8, 16, 24, 32}) {
        for (bool le : {false, true}) {
            if (bits == 8 && le) continue;
            const int bytesPer = bits / 8;
            Aiff a;
            a.channels = 2;
            a.bits = bits;
            a.compression = le ? "sowt" : nullptr;
            const int frames = 5003;
            a.frames = frames;
            uint32_t seed = 12345u + static_cast<uint32_t>(bits);
            for (int i = 0; i < frames * 2 * bytesPer; ++i) {
                seed = seed * 1664525u + 1013904223u;
                a.data.push_back(static_cast<uint8_t>(seed >> 24));
            }
            auto r = openBytes(build(a));
            check(r != nullptr, "opens " + std::to_string(bits) + "-bit");
            if (!r) continue;
            std::vector<int16_t> out;
            int16_t chunk[1001];
            int n;
            while ((n = r->readS16(chunk, 1001)) > 0) {
                check(n % 2 == 0, "a read is whole frames while the file has them");
                out.insert(out.end(), chunk, chunk + n);
            }
            check(static_cast<int>(out.size()) == frames * 2, "every sample read once");
            bool same = true;
            for (int i = 0; i < frames * 2 && i < static_cast<int>(out.size()); ++i) {
                same = same && out[static_cast<size_t>(i)] == kotlinSample(a.data.data(), i * bytesPer, bytesPer, bits, le);
            }
            check(same, std::to_string(bits) + (le ? "-bit sowt" : "-bit") + " samples equal the Kotlin rule");
            check(r->progress() == 1.0f && r->readS16(chunk, 1001) == -1, "the end, and progress 1");
        }
    }
    check(AiffReader::toS16(std::vector<uint8_t>{0x80}.data(), 8, false) == -32768, "8-bit -128 is -32768");
    const uint8_t le24[] = {0x11, 0x22, 0x33};
    check(AiffReader::toS16(le24, 24, true) == 0x3322 && AiffReader::toS16(le24, 24, false) == 0x1122,
          "24-bit keeps the top two bytes in either order");

    // The COMM count stops a read short of trailing bytes; a zero count reads to the end.
    Aiff counted;
    counted.channels = 1;
    counted.frames = 3;
    counted.data = {0, 1, 0, 2, 0, 3, 0, 4, 0, 5};
    auto r = openBytes(build(counted));
    int16_t buf[16] = {};
    check(r && r->readS16(buf, 16) == 3 && buf[2] == 3 && r->readS16(buf, 16) == -1, "the frame count ends the read");
    counted.frames = 0;
    auto open = openBytes(build(counted));
    check(open && open->readS16(buf, 16) == 5 && open->progress() == 0.0f, "no count reads to the end, progress 0");
    Aiff half;
    half.channels = 2;
    half.frames = 4;
    half.data = std::vector<uint8_t>(16, 0);
    auto h = openBytes(build(half));
    check(h && h->readS16(buf, 3) == 2 && h->progress() == 0.25f, "capacity rounds down to whole frames; progress");
}

void fdSource() {
    Aiff a;
    a.channels = 1;
    a.frames = 4;
    a.data = {0, 1, 0, 2, 0, 3, 0, 4};
    auto bytes = build(a);
    char path[] = "/tmp/track_feed_testXXXXXX";
    const int fd = mkstemp(path);
    check(fd >= 0, "temp file");
    if (fd < 0) return;
    // An asset's lead-in before the file's span, and bytes after it. Sized once: GCC 15's
    // -Warray-bounds misreads a small vector grown by insert() as a write past its first buffer.
    std::vector<uint8_t> padded(7 + bytes.size() + 5, 0xCD);
    std::fill_n(padded.begin(), 7, uint8_t{0xAB});
    std::copy(bytes.begin(), bytes.end(), padded.begin() + 7);
    check(write(fd, padded.data(), padded.size()) == static_cast<ssize_t>(padded.size()), "write temp file");
    // The descriptor is left at the end, as a shared one may be: the span's offset is from the file's start.
    geode_aiff* h = geode_aiff_open_fd(fd, 7, static_cast<int64_t>(bytes.size()));
    int16_t buf[8] = {};
    check(h && geode_aiff_channels(h) == 1 && geode_aiff_sample_rate(h) == 44100 && geode_aiff_total_frames(h) == 4,
          "an fd span opens through the C ABI");
    check(geode_aiff_read_s16(h, buf, 8) == 4 && buf[3] == 4 && geode_aiff_progress(h) == 1.0f, "and reads");
    geode_aiff_close(h);
    check(fcntl(fd, F_GETFD) == -1, "the reader closed the descriptor");

    const int notAiff = open(path, O_RDONLY);
    check(geode_aiff_open_fd(notAiff, 0, -1) == nullptr, "a file with a lead-in is not an AIFF from its start");
    check(fcntl(notAiff, F_GETFD) == -1, "a refused descriptor is closed too");
    unlink(path);
}

// ── TrackFeed ────────────────────────────────────────────────────────────────────────────────────────────

struct Heard {
    std::vector<float> samples;
    std::vector<int> calls;
    int channels = 0;
    int rate = 0;
};

void sink(void* context, const float* interleaved, int frames, int channels, int sampleRate) {
    auto* h = static_cast<Heard*>(context);
    h->samples.insert(h->samples.end(), interleaved, interleaved + static_cast<size_t>(frames) * channels);
    h->calls.push_back(frames);
    h->channels = channels;
    h->rate = sampleRate;
}

std::vector<uint8_t> s16Bytes(const std::vector<int16_t>& v) {
    std::vector<uint8_t> b;
    for (int16_t s : v) {
        b.push_back(static_cast<uint8_t>(s & 0xFF));
        b.push_back(static_cast<uint8_t>((static_cast<uint16_t>(s) >> 8) & 0xFF));
    }
    return b;
}

void conversion() {
    TrackFeed f(0);
    check(!f.hasRate(), "no rate before a chunk");
    const std::vector<int16_t> pcm = {0, 16384, -32768, 32767, -1, 1};
    const auto bytes = s16Bytes(pcm);
    f.pushDecoded(bytes.data(), bytes.size(), false, 2, 48000, 0);
    check(f.hasRate() && f.heldFrames() == 3, "a 16-bit chunk is held");
    f.setTargetMs(1000);
    Heard h;
    check(!f.deliver(sink, &h), "the chunk runs out before a second");
    bool same = h.samples.size() == pcm.size();
    for (size_t i = 0; same && i < pcm.size(); ++i) same = h.samples[i] == static_cast<float>(pcm[i]) / 32768.0f;
    check(same && h.channels == 2 && h.rate == 48000, "16-bit is x / 32768, as TrackAudio's PCM16 divide");

    TrackFeed g(0);
    const float floats[] = {0.25f, -0.5f, 1.5f, -0.0f};
    g.pushDecoded(reinterpret_cast<const uint8_t*>(floats), sizeof(floats) + 3, true, 1, 44100, 0);
    g.setTargetMs(1);
    Heard hf;
    g.deliver(sink, &hf);
    check(hf.samples.size() == 4 && hf.samples[2] == 1.5f && std::signbit(hf.samples[3]), "float is copied as is");
}

void codecPreRoll() {
    // Start at 1000 ms; the decoder's chunk starts at 990 ms (the keyframe before): 10 ms at 48 kHz = 480 frames go.
    TrackFeed f(1000);
    std::vector<int16_t> pcm(1000 * 2);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>(i);
    const auto bytes = s16Bytes(pcm);
    f.pushDecoded(bytes.data(), bytes.size(), false, 2, 48000, 990'000);
    check(f.heldFrames() == 520, "(start - pts) * rate / 1e6 frames are dropped");
    f.setTargetMs(10);  // 480 frames into the range
    Heard h;
    check(f.deliver(sink, &h), "the target is reached inside the chunk");
    check(h.samples.size() == 960 && h.samples[0] == static_cast<float>(960) / 32768.0f,
          "frame 0 of the range is the first sample at the start");
    check(f.heldFrames() == 40 && f.delivered() == 480, "the rest is held for the next frame");

    TrackFeed early(1000);
    early.pushDecoded(bytes.data(), bytes.size(), false, 2, 48000, 500'000);
    check(early.heldFrames() == 0 && !early.hasRate(), "a chunk wholly before the start holds nothing");
    TrackFeed odd(7);
    // (7000 - 0) * 44100 / 1e6 = 308.7 -> 308, Long division as the Kotlin.
    std::vector<int16_t> mono(400, 1);
    const auto mb = s16Bytes(mono);
    odd.pushDecoded(mb.data(), mb.size(), false, 1, 44100, 0);
    check(odd.heldFrames() == 92, "the cut truncates toward zero");
    // 12 000 s of pre-roll at 192 kHz is 2.3e9 frames, past int32: the whole chunk goes, nothing more.
    TrackFeed far(12'000'000);
    far.pushDecoded(mb.data(), mb.size(), false, 1, 192000, 0);
    check(far.heldFrames() == 0, "a cut longer than 2^31 frames drops the chunk and no more");
}

void frameExactHandOut() {
    // Video frames at 30 fps over 44.1 kHz chunks of 1152 frames: each frame hears up to endMs * rate / 1000.
    TrackFeed f(0);
    Heard h;
    int64_t written = 0;
    int pushes = 0;
    for (int frame = 0; frame < 300; ++frame) {
        const int64_t endMs = (static_cast<int64_t>(frame) + 1) * 1000 / 30;
        while (!f.hasRate()) {
            std::vector<int16_t> chunk(1152 * 2, 3);
            const auto b = s16Bytes(chunk);
            f.pushDecoded(b.data(), b.size(), false, 2, 44100, written * 1'000'000 / 44100);
            written += 1152;
            ++pushes;
        }
        f.setTargetMs(endMs);
        while (!f.deliver(sink, &h)) {
            std::vector<int16_t> chunk(1152 * 2, 3);
            const auto b = s16Bytes(chunk);
            f.pushDecoded(b.data(), b.size(), false, 2, 44100, written * 1'000'000 / 44100);
            written += 1152;
            ++pushes;
        }
        check(f.delivered() == endMs * 44100 / 1000, "frame " + std::to_string(frame) + " hears exactly its audio");
    }
    int64_t total = 0;
    for (int c : h.calls) total += c;
    check(total == f.delivered() && static_cast<int64_t>(h.samples.size()) == total * 2, "every frame once, in order");
    check(pushes == static_cast<int>((f.delivered() + 1151) / 1152), "no chunk decoded before it is needed");
}

void aiffSkip() {
    // 2 s of mono at 8 kHz, range from 1500 ms: 12000 frames skipped across two 8192-sample reads.
    Aiff a;
    a.channels = 1;
    a.rate = 8000;
    a.frames = 16000;
    for (int i = 0; i < 16000; ++i) be16(a.data, i);
    auto r = openBytes(build(a));
    TrackFeed f(1500);
    check(r && !f.hasRate(), "no rate before the first read");
    int reads = 0;
    while (r && !f.hasRate()) {
        check(f.pushAiff(*r), "reads while the file lasts");
        ++reads;
    }
    // The second read is cut to the COMM count: 16000 - 8192 = 7808 samples, 3808 of them still skipped.
    check(reads == 2 && f.heldFrames() == 16000 - 12000, "startMs * rate / 1000 frames skipped, then held");
    f.setTargetMs(250);
    Heard h;
    check(f.deliver(sink, &h) && h.samples.size() == 2000 && h.samples[0] == 12000.0f / 32768.0f,
          "the range's first frame is the first after the skip");
    Heard rest;
    f.setTargetMs(100'000);
    check(!f.deliver(sink, &rest) && rest.samples.size() == 2000, "the rest, then it runs out");
    check(r && !f.pushAiff(*r), "then the end");
    check(f.delivered() == 4000, "all 0.5 s after the start, once");
}

void cAbi() {
    geode_track_feed* f = geode_track_feed_create(0);
    check(geode_track_feed_has_rate(f) == 0, "C ABI: no rate");
    const auto b = s16Bytes({100, 200, 300, 400});
    geode_track_feed_push_decoded(f, b.data(), b.size(), 0, 2, 1000, 0);
    geode_track_feed_target_ms(f, 1);  // 1 frame at 1 kHz
    Heard h;
    check(geode_track_feed_has_rate(f) == 1 && geode_track_feed_deliver(f, sink, &h) == 1 && h.samples.size() == 2,
          "C ABI: one frame");
    geode_track_feed_target_ms(f, 5);
    check(geode_track_feed_deliver(f, sink, &h) == 0 && h.samples.size() == 4, "C ABI: runs out");
    check(geode_track_feed_push_aiff(f, nullptr) == 0 && geode_track_feed_deliver(nullptr, sink, &h) == 0,
          "C ABI: null handles");
    geode_track_feed_destroy(f);
    geode_track_feed_destroy(nullptr);
    geode_aiff_close(nullptr);
}

}  // namespace

int main() {
    headers();
    samples();
    fdSource();
    conversion();
    codecPreRoll();
    frameExactHandOut();
    aiffSkip();
    cAbi();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("track_feed_test: all passed\n");
    return 0;
}
