// Host checks for the native ingest ring (core/audio/ingest, #480). Built and run by CI ("Native host tests"
// in .github/workflows/android.yml) with the system compiler; no device.
//
// The writer side is driven through the C ABI in the order the tap and the capture pumps will call it
// (geode_ingest_begin_epoch / set_format / write); the reader side is the native Cursor every renderer and
// Group D's hop analyzer will own. What is pinned:
// - every sample read once, in order, each with the rate it was written at, for random chunk sizes on both
//   sides and a rate change mid-stream; the same for two readers at once, and for a real writer thread;
// - an overrun skips to the oldest safe sample, reports what it dropped, and never hands out a torn sample;
// - a new epoch restarts readers, a format change inside one (speed, pitch) does not;
// - int16, u8, float (and 24- and 32-bit) PCM convert to the same floats;
// - the BS.775 downmix weights for 5.1 and 7.1, stereo bit-exact, mono centred;
// - the raw mono tail projectM takes ends at the cursor and stays inside the epoch;
// - the ring's silence answer rests after the hold, and a ring nobody writes rests too; it is a pure read of
//   the writer's stamp, so pollers on several threads, and one that starts late, get one answer (#491);
// - the ring's UI level is the newest samples' RMS while written, 0 once the ring stops (#491);
// - switching sources player → mic → player (#485): each switch restarts the reader, and no frame is read
//   at the other source's rate.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <thread>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/Cursor.hpp"
#include "audio/ingest/Ring.hpp"
#include "sound/Silence.hpp"

using geode::audio::ingest::Cursor;
using geode::audio::ingest::Ring;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

// A stereo int16 signal whose every frame says where it is: the left sample of position p is left(p), so a
// skipped, repeated, reordered or torn frame cannot pass for the right one. 7919 is prime, so neighbours and
// the frame one ring capacity earlier never share a value.
int16_t left(int64_t p) {
    return static_cast<int16_t>((p * 7919 + 13) % 65536 - 32768);
}
int16_t right(int64_t p) {
    return static_cast<int16_t>((p * 104729 + 7) % 65536 - 32768);
}

void writeStereo16(geode_ingest* ing, int64_t from, int frames) {
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        pcm[2 * static_cast<size_t>(i)] = left(from + i);
        pcm[2 * static_cast<size_t>(i) + 1] = right(from + i);
    }
    geode_ingest_write(ing, pcm.data(), static_cast<size_t>(frames), GEODE_PCM_S16);
}

bool frameMatches(const float* stereo, int64_t p) {
    return stereo[0] == static_cast<float>(left(p)) / 32768.0f && stereo[1] == static_cast<float>(right(p)) / 32768.0f;
}

Ring& ringOf(geode_ingest* ing) {
    return ing->ring;
}

// One reader checking the stream it gets: contiguous positions (except across what it was told it
// dropped), every frame the one written there, and the rate that position was written at.
struct Checker {
    Cursor cursor;
    int64_t next;
    int64_t frames = 0;
    int64_t dropped = 0;
    bool ok = true;
    std::vector<float> buf;

    Checker(const Ring& ring, int maxFrames)
        : cursor(ring, Cursor::Start::Oldest), next(cursor.position()), buf(static_cast<size_t>(maxFrames) * 2) {}

    // rateAt(p) is the rate position p was written at.
    template <typename RateAt>
    Cursor::Block take(int maxFrames, RateAt rateAt) {
        const Cursor::Block b = cursor.read(buf.data(), maxFrames);
        dropped += b.dropped;
        next += b.dropped;
        if (b.frames == 0) return b;
        if (b.position != next) ok = false;
        for (int i = 0; i < b.frames; ++i) {
            if (!frameMatches(&buf[2 * static_cast<size_t>(i)], b.position + i)) ok = false;
            if (rateAt(b.position + i) != b.sampleRate) ok = false;
        }
        next = b.position + b.frames;
        frames += b.frames;
        return b;
    }
};

// Random chunk sizes on both sides, a rate change mid-stream, two readers at different paces.
void everySampleOnceInOrderWithItsRate() {
    geode_ingest* ing = geode_ingest_create(1 << 14, 2);
    check(ing != nullptr, "ring created");
    Ring& ring = ringOf(ing);
    std::mt19937 rng(480);
    std::uniform_int_distribution<int> chunk(1, 2000);
    std::uniform_int_distribution<int> take(1, 3000);

    constexpr int64_t kChange = 200'000;   // the rate changes here, mid-chunk
    auto rateAt = [](int64_t p) { return p < kChange ? 44100 : 48000; };

    geode_ingest_begin_epoch(ing, 44100, 2, 0);
    Checker a(ring, 3000);
    Checker b(ring, 3000);
    int64_t written = 0;
    bool restartedOnce = true;
    bool changed = false;
    int rateBlocksAfter = 0;
    for (int iteration = 0; written < 400'000; ++iteration) {
        int n = chunk(rng);
        if (!changed && written + n >= kChange) {
            changed = true;
            const int head = static_cast<int>(kChange - written);
            writeStereo16(ing, written, head);
            written += head;
            geode_ingest_set_format(ing, 48000, 2, 0);
            n -= head;
        }
        writeStereo16(ing, written, n);
        written += n;
        // Reader a drains every time; reader b only now and then, in larger bites.
        for (;;) {
            const Cursor::Block blk = a.take(take(rng), rateAt);
            if (blk.frames == 0) break;
            if (blk.restarted && blk.position != 0) restartedOnce = false;
            if (blk.position >= kChange) ++rateBlocksAfter;
        }
        // Every fourth write: at most 8000 frames behind, inside the 12288 a 16384-frame ring keeps safe.
        if (iteration % 4 == 3) {
            while (b.take(take(rng), rateAt).frames > 0) {
            }
        }
    }
    while (b.take(3000, rateAt).frames > 0) {
    }
    check(geode_ingest_written(ing) == written, "written counts every frame");
    check(a.ok && a.frames == written && a.dropped == 0, "reader a: every frame once, in order, with its rate");
    check(b.ok && b.frames == written && b.dropped == 0, "reader b: every frame once, in order, with its rate");
    check(restartedOnce, "only the first block restarts: a rate change is not an epoch");
    check(rateBlocksAfter > 0, "blocks after the change carry the new rate");
    geode_ingest_destroy(ing);
}

// A block never spans a format boundary, so one block has one rate.
void blockStopsAtAFormatBoundary() {
    geode_ingest* ing = geode_ingest_create(4096, 2);
    geode_ingest_begin_epoch(ing, 44100, 2, 0);
    writeStereo16(ing, 0, 100);
    geode_ingest_set_format(ing, 88200, 2, 0);
    writeStereo16(ing, 100, 50);
    Cursor c(ringOf(ing), Cursor::Start::Oldest);
    std::vector<float> out(2 * 1000);
    const Cursor::Block first = c.read(out.data(), 1000);
    const Cursor::Block second = c.read(out.data(), 1000);
    check(first.frames == 100 && first.sampleRate == 44100, "first block ends at the format change");
    check(second.frames == 50 && second.sampleRate == 88200 && second.position == 100, "second block in the new rate");
    check(!second.restarted && second.epoch == first.epoch, "a format change keeps the epoch");
    geode_ingest_destroy(ing);
}

void epochRestartsAndSetFormatDoesNot() {
    geode_ingest* ing = geode_ingest_create(4096, 2);
    Ring& ring = ringOf(ing);
    Cursor c(ring, Cursor::Start::Newest);
    std::vector<float> out(2 * 512);

    geode_ingest_begin_epoch(ing, 44100, 2, 0);
    writeStereo16(ing, 0, 64);
    Cursor::Block b = c.read(out.data(), 512);
    check(b.frames == 64 && b.restarted, "a cursor's first block restarts");
    const uint32_t first = b.epoch;

    // Speed or pitch: Media3 flushes with a new format, same stream.
    geode_ingest_set_format(ing, 32000, 2, 0);
    writeStereo16(ing, 64, 64);
    b = c.read(out.data(), 512);
    check(b.frames == 64 && !b.restarted && b.epoch == first && b.sampleRate == 32000, "set_format: no restart");

    // The same format again publishes nothing.
    const int64_t records = ring.records();
    geode_ingest_set_format(ing, 32000, 2, 0);
    check(ring.records() == records, "repeating the format publishes no record");

    // A seek: a new epoch, even at the same format.
    geode_ingest_begin_epoch(ing, 32000, 2, 0);
    writeStereo16(ing, 128, 64);
    b = c.read(out.data(), 512);
    check(b.frames == 64 && b.restarted && b.epoch != first, "begin_epoch restarts, same format or not");
    check(frameMatches(out.data(), 128), "the new epoch's first frame");

    // Two epochs back to back with nothing between: the reader still restarts once on the next sample.
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    writeStereo16(ing, 192, 10);
    b = c.read(out.data(), 512);
    check(b.frames == 10 && b.restarted && b.sampleRate == 48000, "empty epoch skipped, the next restarts");
    b = c.read(out.data(), 512);
    check(b.frames == 0 && !b.restarted, "nothing new, nothing read");

    // A writer before any format: dropped, not misread.
    geode_ingest* fresh = geode_ingest_create(1024, 2);
    writeStereo16(fresh, 0, 32);
    check(geode_ingest_written(fresh) == 0, "writes before a format are dropped");
    geode_ingest_destroy(fresh);
    geode_ingest_destroy(ing);
}

// A reader left behind skips to the oldest safe sample, reports the gap, and goes on in order.
void overrunSkipsToOldestSafe() {
    geode_ingest* ing = geode_ingest_create(4096, 2);
    Ring& ring = ringOf(ing);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    Cursor c(ring, Cursor::Start::Oldest);
    int64_t written = 0;
    while (written < 3 * 4096 + 77) {
        writeStereo16(ing, written, 500);
        written += 500;
    }
    std::vector<float> out(2 * 8192);
    const int64_t safe = ring.oldestSafe(written);
    Cursor::Block b = c.read(out.data(), 8192);
    check(safe == written + ring.runway() - ring.capacity(), "oldest safe keeps a runway from the pen");
    check(b.dropped == safe && b.position == safe, "overrun: skips to the oldest safe sample, reports the drop");
    check(b.frames == written - safe, "then reads everything still held");
    bool ok = true;
    for (int i = 0; i < b.frames; ++i) ok = ok && frameMatches(&out[2 * static_cast<size_t>(i)], b.position + i);
    check(ok, "every frame after the skip is the one written there");
    check(!b.restarted || b.position == safe, "an overrun is not an epoch");

    writeStereo16(ing, written, 10);
    b = c.read(out.data(), 8192);
    check(b.frames == 10 && b.dropped == 0 && b.position == written, "then on in order with no drop");

    // skipToNewest: a live surface catching up on what plays now.
    written += 10;
    writeStereo16(ing, written, 1000);
    written += 1000;
    check(c.behind() == 1000, "behind counts unread frames");
    check(c.skipToNewest(100) == 900, "skips all but the newest 100");
    b = c.read(out.data(), 8192);
    check(b.frames == 100 && b.dropped == 900 && b.position == written - 100, "the skip shows as dropped");
    geode_ingest_destroy(ing);
}

// A real writer thread racing a real reader: whatever the reader gets is in order and never torn.
void concurrentWriterNeverTears() {
    geode_ingest* ing = geode_ingest_create(1 << 13, 2);
    Ring& ring = ringOf(ing);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    constexpr int64_t kTotal = 3'000'000;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        std::mt19937 rng(7);
        std::uniform_int_distribution<int> chunk(1, 1500);
        int64_t w = 0;
        while (w < kTotal) {
            const int n = static_cast<int>(std::min<int64_t>(chunk(rng), kTotal - w));
            writeStereo16(ing, w, n);
            w += n;
            if ((w & 0x3FFF) < 1500) std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });
    Checker r(ring, 2048);
    std::mt19937 rng(11);
    std::uniform_int_distribution<int> take(1, 2048);
    auto rateAt = [](int64_t) { return 48000; };
    for (;;) {
        const bool finished = done.load(std::memory_order_acquire);
        const Cursor::Block b = r.take(take(rng), rateAt);
        if (finished && b.frames == 0) break;
    }
    writer.join();
    check(r.ok, "concurrent: every frame read is the one written there, in order");
    check(r.frames + r.dropped == kTotal, "concurrent: read plus dropped is everything written");
    std::printf("concurrent: read %lld, dropped %lld of %lld\n", static_cast<long long>(r.frames),
                static_cast<long long>(r.dropped), static_cast<long long>(kTotal));
    geode_ingest_destroy(ing);
}

// Reads the one stereo frame a mono or multichannel write produced.
void readOne(Cursor& c, float& l, float& r) {
    float out[2] = {0.0f, 0.0f};
    const Cursor::Block b = c.read(out, 1);
    l = b.frames == 1 ? out[0] : NAN;
    r = b.frames == 1 ? out[1] : NAN;
}

void conversions() {
    geode_ingest* ing = geode_ingest_create(1024, 1);
    geode_ingest_begin_epoch(ing, 48000, 1, 0);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    float l = 0.0f;
    float r = 0.0f;

    const int16_t s16[] = {0, 32767, -32768, 16384};
    const float s16Want[] = {0.0f, 32767.0f / 32768.0f, -1.0f, 0.5f};
    geode_ingest_write(ing, s16, 4, GEODE_PCM_S16);
    bool ok = true;
    for (float want : s16Want) {
        readOne(c, l, r);
        ok = ok && l == want && r == want;
    }
    check(ok, "int16: s / 32768, the scaling the Kotlin tap used");

    const uint8_t u8[] = {128, 255, 0, 192};
    const float u8Want[] = {0.0f, 127.0f / 128.0f, -1.0f, 0.5f};
    geode_ingest_write(ing, u8, 4, GEODE_PCM_U8);
    ok = true;
    for (float want : u8Want) {
        readOne(c, l, r);
        ok = ok && l == want && r == want;
    }
    check(ok, "u8: (s - 128) / 128");

    const float f32[] = {0.25f, -0.5f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    const float f32Want[] = {0.25f, -0.5f, 0.0f, 0.0f};
    geode_ingest_write(ing, f32, 4, GEODE_PCM_F32);
    ok = true;
    for (float want : f32Want) {
        readOne(c, l, r);
        ok = ok && l == want && r == want;
    }
    check(ok, "float: as written, NaN and infinity to silence");

    const uint8_t s24[] = {0x00, 0x00, 0x40, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF};   // 0.5, -1, -1 LSB
    const float s24Want[] = {0.5f, -1.0f, -1.0f / 8388608.0f};
    geode_ingest_write(ing, s24, 3, GEODE_PCM_S24_PACKED);
    ok = true;
    for (float want : s24Want) {
        readOne(c, l, r);
        ok = ok && l == want;
    }
    check(ok, "24-bit packed: little-endian, sign-extended");

    const int32_t s32[] = {0x40000000, std::numeric_limits<int32_t>::min()};
    const float s32Want[] = {0.5f, -1.0f};
    geode_ingest_write(ing, s32, 2, GEODE_PCM_S32);
    ok = true;
    for (float want : s32Want) {
        readOne(c, l, r);
        ok = ok && l == want;
    }
    check(ok, "32-bit: s / 2^31");

    const int64_t before = geode_ingest_written(ing);
    geode_ingest_write(ing, s16, 4, 99);
    check(geode_ingest_written(ing) == before, "an unknown encoding writes nothing");

    // write_bytes: only whole frames.
    geode_ingest_begin_epoch(ing, 48000, 1, 0);
    check(geode_ingest_write_bytes(ing, s16, 7, GEODE_PCM_S16) == 3, "write_bytes takes the whole frames");
    geode_ingest_destroy(ing);
}

constexpr float kC = 0.70710678f;
constexpr float kLfe = 0.31622777f;

// Writes one float frame per channel with only that channel at 1 and checks each downmixed frame.
void checkWeights(int channels, int mask, const float (*want)[2], const char* what) {
    geode_ingest* ing = geode_ingest_create(1024, 8);
    geode_ingest_begin_epoch(ing, 48000, channels, mask);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    std::vector<float> pcm(static_cast<size_t>(channels * channels), 0.0f);
    for (int k = 0; k < channels; ++k) pcm[static_cast<size_t>(k * channels + k)] = 1.0f;
    geode_ingest_write(ing, pcm.data(), static_cast<size_t>(channels), GEODE_PCM_F32);
    std::vector<float> out(static_cast<size_t>(2 * channels));
    const Cursor::Block b = c.read(out.data(), channels);
    bool ok = b.frames == channels && b.sourceChannels == channels;
    for (int k = 0; k < channels && ok; ++k) {
        ok = near(out[2 * static_cast<size_t>(k)], want[k][0]) && near(out[2 * static_cast<size_t>(k) + 1], want[k][1]);
        if (!ok) std::printf("  channel %d -> %f %f, want %f %f\n", k, out[2 * k], out[2 * k + 1], want[k][0], want[k][1]);
    }
    check(ok, what);
    geode_ingest_destroy(ing);
}

void downmixWeights() {
    // 5.1, Android's order FL FR FC LFE BL BR.
    const float fivePointOne[6][2] = {{1, 0}, {0, 1}, {kC, kC}, {kLfe, kLfe}, {kC, 0}, {0, kC}};
    checkWeights(6, 0xFC, fivePointOne, "5.1: C and surrounds -3 dB, LFE -10 dB");
    checkWeights(6, 0, fivePointOne, "5.1 with no mask: the default layout for six channels");
    // 7.1: FL FR FC LFE BL BR SL SR.
    const float sevenPointOne[8][2] = {{1, 0}, {0, 1}, {kC, kC}, {kLfe, kLfe}, {kC, 0}, {0, kC}, {kC, 0}, {0, kC}};
    checkWeights(8, 0x18FC, sevenPointOne, "7.1: sides -3 dB too");
    // Stereo passes untouched.
    const float stereo[2][2] = {{1, 0}, {0, 1}};
    checkWeights(2, 0, stereo, "stereo: identity");

    // Stereo bit-exact with arbitrary values (the golden sound frames depend on it).
    geode_ingest* ing = geode_ingest_create(1024, 8);
    geode_ingest_begin_epoch(ing, 48000, 2, 0xC);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    const float pcm[] = {0.123456789f, -0.987654321f, 1e-30f, -0.0f};
    geode_ingest_write(ing, pcm, 2, GEODE_PCM_F32);
    float out[4];
    c.read(out, 2);
    check(std::memcmp(out, pcm, sizeof pcm) == 0, "stereo float is bit-exact through the ring");
    geode_ingest_destroy(ing);

    // A source with more channels than the ring keeps: its first ones, still a correct layout.
    geode_ingest* narrow = geode_ingest_create(1024, 2);
    geode_ingest_begin_epoch(narrow, 48000, 6, 0xFC);
    Cursor n(ringOf(narrow), Cursor::Start::Newest);
    const float six[] = {0.5f, -0.25f, 1.0f, 1.0f, 1.0f, 1.0f};
    geode_ingest_write(narrow, six, 1, GEODE_PCM_F32);
    const Cursor::Block b = n.read(out, 1);
    check(b.frames == 1 && out[0] == 0.5f && out[1] == -0.25f && b.sourceChannels == 6,
          "past max_channels: the first channels, read as front left and right");
    geode_ingest_destroy(narrow);
}

void monoCentred() {
    geode_ingest* ing = geode_ingest_create(1024, 8);
    geode_ingest_begin_epoch(ing, 44100, 1, 0x4);   // Android calls mono FRONT_LEFT
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    const float pcm[] = {0.5f, -0.3f};
    geode_ingest_write(ing, pcm, 2, GEODE_PCM_F32);
    float out[4];
    const Cursor::Block b = c.read(out, 2);
    check(b.frames == 2 && out[0] == 0.5f && out[1] == 0.5f && out[2] == -0.3f && out[3] == -0.3f,
          "mono: duplicated to both sides at unity, not hard left");
    float tail[2];
    check(c.rawMonoTail(tail, 2) == 2 && tail[0] == 0.5f && tail[1] == -0.3f, "mono raw tail is the mono itself");
    geode_ingest_destroy(ing);
}

void rawMonoTail() {
    geode_ingest* ing = geode_ingest_create(4096, 2);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    float tail[600];
    check(c.rawMonoTail(tail, 600) == 0, "no tail before the first read");

    writeStereo16(ing, 0, 300);
    std::vector<float> out(2 * 1000);
    c.read(out.data(), 200);   // the cursor is at 200; 100 more are written but unread
    const int n = c.rawMonoTail(tail, 64);
    bool ok = n == 64;
    for (int i = 0; i < n && ok; ++i) {
        const int64_t p = 200 - 64 + i;
        const float want = 0.5f * (static_cast<float>(left(p)) / 32768.0f + static_cast<float>(right(p)) / 32768.0f);
        ok = near(tail[i], want, 1e-7f);
    }
    check(ok, "the tail is the newest frames the cursor read, (L + R) / 2, oldest first");
    check(c.rawMonoTail(tail, 600) == 200, "the tail stops at what the ring has");

    // Across a format change inside the epoch it continues; across an epoch it stops.
    c.read(out.data(), 1000);
    geode_ingest_set_format(ing, 44100, 2, 0);
    writeStereo16(ing, 300, 50);
    c.read(out.data(), 1000);
    check(c.rawMonoTail(tail, 600) == 350, "the tail spans a format change");
    geode_ingest_begin_epoch(ing, 44100, 2, 0);
    writeStereo16(ing, 350, 40);
    c.read(out.data(), 1000);
    check(c.rawMonoTail(tail, 600) == 40, "the tail never reaches into the previous epoch");
    geode_ingest_destroy(ing);
}

// The ring's clock in these tests: the time the writer stamps audible chunks with. Atomic because the
// concurrent check reads it from a writer thread.
std::atomic<int64_t> fakeNowMs{0};
int64_t fakeClock() {
    return fakeNowMs.load();
}

void writeAt(geode_ingest* ing, int64_t nowMs, const float* pcm, size_t frames) {
    fakeNowMs = nowMs;
    geode_ingest_write(ing, pcm, frames, GEODE_PCM_F32);
}

void restingAfterTheHold() {
    fakeNowMs = 0;
    geode_ingest* ing = geode_ingest_create(4096, 2);
    ing->ring.setClock(&fakeClock);
    geode_ingest_begin_epoch(ing, 48000, 1, 0);
    float loud[64] = {};
    loud[0] = 0.5f;
    const float quiet[64] = {};
    check(geode_ingest_resting(ing, 0) == 0, "a new ring counts as just heard");
    writeAt(ing, 1000, loud, 64);
    check(geode_ingest_resting(ing, 1000) == 0, "audible: awake");
    check(geode_ingest_resting(ing, 6900) == 0, "within the hold: awake");
    check(geode_ingest_resting(ing, 7001) == 1, "after the hold: may rest");
    writeAt(ing, 8000, quiet, 64);
    check(geode_ingest_resting(ing, 8000) == 1, "digital silence written: still resting");
    writeAt(ing, 8100, loud, 64);
    check(geode_ingest_resting(ing, 8100) == 0, "sound wakes it at once");
    check(geode_ingest_resting(ing, 14100) == 0, "no writes, within the hold");
    check(geode_ingest_resting(ing, 14101) == 1, "no writes for the hold: silent");

    fakeNowMs = 0;
    geode_ingest* idle = geode_ingest_create(1024, 2);
    idle->ring.setClock(&fakeClock);
    check(geode_ingest_resting(idle, 5000) == 0 && geode_ingest_resting(idle, 6001) == 1,
          "a ring nobody writes rests the hold after it was made");
    check(geode_ingest_resting(nullptr, 0) == 1, "no ring rests");
    geode_ingest_destroy(idle);
    geode_ingest_destroy(ing);
}

// The wallpaper, the dream, the edge glow and the capture screen poll one ring, each on its own schedule
// (#491). The answer is the writer's stamp against the poller's now, so it never depends on who polled
// first, how often, or on which thread.
void restingIsOneAnswerForManyPollers() {
    fakeNowMs = 0;
    geode_ingest* ing = geode_ingest_create(4096, 2);
    ing->ring.setClock(&fakeClock);
    geode_ingest_begin_epoch(ing, 48000, 1, 0);
    float loud[64] = {};
    loud[0] = 0.5f;
    writeAt(ing, 20000, loud, 64);

    // A poller that starts late agrees with one that polled all along: the answer has no poll-side state.
    for (int64_t t = 20000; t <= 26000; t += 250) check(geode_ingest_resting(ing, t) == 0, "the regular poller: awake");
    check(geode_ingest_resting(ing, 26001) == 1, "the regular poller: resting after the hold");
    geode_ingest* fresh = geode_ingest_create(4096, 2);
    fresh->ring.setClock(&fakeClock);
    geode_ingest_begin_epoch(fresh, 48000, 1, 0);
    writeAt(fresh, 20000, loud, 64);
    check(geode_ingest_resting(fresh, 26001) == 1,
          "a first poll gets no fresh grace: it sees how long the ring has really been quiet");
    geode_ingest_destroy(fresh);

    // Two poller threads while the writer keeps writing sound stamped at 30000: every answer is a function
    // of the poller's now alone.
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        while (!stop.load()) writeAt(ing, 30000, loud, 64);
    });
    while (geode_ingest_resting(ing, 30000) != 0) std::this_thread::yield();   // the writer has stamped
    std::atomic<int> wrong{0};
    auto poller = [&](int64_t offset) {
        for (int64_t i = 0; i < 20000; ++i) {
            const int64_t now = 30000 + (i * 7 + offset) % 12000;
            const int expected = now - 30000 > geode::sound::silence::kRestAfterMs ? 1 : 0;
            if (geode_ingest_resting(ing, now) != expected) ++wrong;
        }
    };
    std::thread a(poller, 0);
    std::thread b(poller, 3);
    a.join();
    b.join();
    stop = true;
    writer.join();
    check(wrong.load() == 0, "two poller threads get one answer while the writer runs");
    geode_ingest_destroy(ing);
}

void writeSine(geode_ingest* ing, int frames, float amplitude, int64_t& phase) {
    std::vector<float> pcm(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i, ++phase) {
        const auto v = static_cast<float>(amplitude * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(phase) / 48000.0));
        pcm[2 * static_cast<size_t>(i)] = v;
        pcm[2 * static_cast<size_t>(i) + 1] = v;
    }
    geode_ingest_write(ing, pcm.data(), static_cast<size_t>(frames), GEODE_PCM_F32);
}

// The edge glow's level (#491): the newest samples' RMS, read natively from the ring the surface hears.
void levelFollowsTheRing() {
    geode_ingest* ing = geode_ingest_create(1 << 14, 2);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    int64_t phase = 0;
    writeSine(ing, 4800, 0.5f, phase);
    check(geode_ingest_level(ing, 0) == 0.0f, "the first poll: what was written before anyone listened is old news");
    writeSine(ing, 480, 0.5f, phase);
    const float level = geode_ingest_level(ing, 10);
    check(level > 0.33f && level < 0.38f, "a 0.5 sine on both sides reads its RMS, 0.354");
    check(near(geode_ingest_level(ing, 10), level), "a second poller at the same moment reads the same level");
    check(geode_ingest_level(ing, 11 + geode::audio::ingest::level::kStaleMs) == 0.0f,
          "no writes for longer than kStaleMs: 0, so a paused tap never freezes the glow");
    writeSine(ing, 480, 0.0f, phase);
    writeSine(ing, 480, 0.0f, phase);
    check(geode_ingest_level(ing, 500) == 0.0f, "written silence reads 0");
    writeSine(ing, 480, 0.5f, phase);
    check(geode_ingest_level(ing, 520) > 0.1f, "sound again: the level comes back on the next poll");
    check(geode_ingest_level(nullptr, 0) == 0.0f, "no ring reads 0");
    geode_ingest_destroy(ing);
}

// More format changes than the record ring holds, unread: the reader resynchronises instead of misreading.
void recordRingOverrun() {
    geode_ingest* ing = geode_ingest_create(1 << 14, 2);
    geode_ingest_begin_epoch(ing, 40000, 2, 0);
    Checker r(ringOf(ing), 4096);
    int64_t written = 0;
    std::vector<int> rates;
    for (int i = 0; i < Ring::kRecords * 2; ++i) {
        rates.push_back(40000 + i);
        if (i > 0) geode_ingest_set_format(ing, 40000 + i, 2, 0);
        writeStereo16(ing, written, 3);
        written += 3;
    }
    auto rateAt = [&](int64_t p) { return rates[static_cast<size_t>(p / 3)]; };
    while (r.take(4096, rateAt).frames > 0) {
    }
    check(r.ok, "after a record overrun: frames and rates still match");
    check(r.dropped > 0 && r.frames + r.dropped == written, "the frames whose records were lost count as dropped");
    geode_ingest_destroy(ing);
}

// Switching the visuals between sources (#485): player → mic → player. Each source writes its own ring
// (AudioBus.micRing beside the player's), the capture pump begins an epoch on its worker before its first
// write, and a renderer handed another ring (geode_viz_set_sound_source) starts a fresh cursor on it. So:
// each switch is a restart, every frame carries the rate of the ring it came from (no mic samples at the
// player's rate or the other way round: no wrong-rate burst), and a cursor never hands out another
// source's samples.
constexpr int kSwitchPlayerRate = 48000;
constexpr int kSwitchMicRate = 44100;
constexpr int kSwitchChunk = 480;
constexpr int16_t kPlayerValue = 8192;   // 0.25
constexpr int16_t kMicValue = -16384;    // -0.5

void writeConstant(geode_ingest* ing, int channels, int16_t value, int chunks) {
    std::vector<int16_t> pcm(static_cast<size_t>(kSwitchChunk) * static_cast<size_t>(channels), value);
    for (int i = 0; i < chunks; ++i) geode_ingest_write(ing, pcm.data(), kSwitchChunk, GEODE_PCM_S16);
}

struct Heard {
    int64_t frames = 0;
    bool restarted = false;
    bool rateRight = true;
    bool valueRight = true;
};

// Reads the cursor dry, as a renderer does over a few frames, checking each block's rate and samples.
Heard drain(Cursor& cursor, int rate, float value) {
    Heard h;
    std::vector<float> stereo(4096 * 2);
    for (int guard = 0; guard < 1000; ++guard) {
        const Cursor::Block b = cursor.read(stereo.data(), 4096);
        if (b.restarted) h.restarted = true;
        if (b.frames == 0) break;
        if (b.sampleRate != rate) h.rateRight = false;
        for (int i = 0; i < b.frames * 2; ++i) {
            if (!near(stereo[static_cast<size_t>(i)], value)) h.valueRight = false;
        }
        h.frames += b.frames;
    }
    return h;
}

void sourceSwitchRestartsWithoutAWrongRateBurst() {
    geode_ingest* player = geode_ingest_create(1 << 15, 8);
    geode_ingest* mic = geode_ingest_create(1 << 15, 2);
    const float playerValue = kPlayerValue / 32768.0f;
    const float micValue = kMicValue / 32768.0f;

    // Playing: the renderer hears the player's ring from now on.
    geode_ingest_begin_epoch(player, kSwitchPlayerRate, 2, 0x0C);
    writeConstant(player, 2, kPlayerValue, 4);
    Cursor onPlayer(ringOf(player));
    writeConstant(player, 2, kPlayerValue, 6);
    const Heard playing = drain(onPlayer, kSwitchPlayerRate, playerValue);
    check(playing.restarted && playing.frames == 6 * kSwitchChunk, "player: a fresh cursor restarts and hears each new frame once");
    check(playing.rateRight && playing.valueRight, "player: its own samples at its own rate");

    // Mic on: playback pauses (no more player writes); the renderer gets the mic's ring while the pump's
    // worker begins its epoch at the rate the AudioRecord opened, then writes mono.
    Cursor onMic(ringOf(mic));
    geode_ingest_begin_epoch(mic, kSwitchMicRate, 1, 0x04);
    writeConstant(mic, 1, kMicValue, 10);
    const Heard micHeard = drain(onMic, kSwitchMicRate, micValue);
    check(micHeard.restarted && micHeard.frames == 10 * kSwitchChunk, "player → mic restarts the renderer, every mic frame once");
    check(micHeard.rateRight, "player → mic: every mic frame at the mic's rate, none at the player's");
    check(micHeard.valueRight, "player → mic: only mic samples, mono on both sides");

    // Mic off: back to the player's ring, then play resumes on a new epoch.
    Cursor backOnPlayer(ringOf(player));
    const Heard idle = drain(backOnPlayer, kSwitchPlayerRate, playerValue);
    check(idle.frames == 0, "mic → player: nothing old replayed while the player is paused");
    geode_ingest_begin_epoch(player, kSwitchPlayerRate, 2, 0x0C);
    writeConstant(player, 2, kPlayerValue, 4);
    const Heard back = drain(backOnPlayer, kSwitchPlayerRate, playerValue);
    check(back.restarted && back.frames == 4 * kSwitchChunk, "mic → player restarts the renderer, every player frame once");
    check(back.rateRight && back.valueRight, "mic → player: player samples at the player's rate, no mic burst");

    // The mic again, now opened at another rate: a cursor still on its ring restarts at the new rate.
    geode_ingest_begin_epoch(mic, 16000, 1, 0x04);
    writeConstant(mic, 1, kMicValue, 2);
    const Heard again = drain(onMic, 16000, micValue);
    check(again.restarted && again.frames == 2 * kSwitchChunk, "a new capture run restarts its reader");
    check(again.rateRight, "a new capture run: every frame at the run's own rate, none at the last run's");

    geode_ingest_destroy(mic);
    geode_ingest_destroy(player);
}

void sourceKind() {
    geode_ingest* ring = geode_ingest_create(1024, 2);
    check(geode_ingest_source_kind(ring) == GEODE_SOURCE_PLAYER, "source kind: a new ring is the player's");
    geode_ingest_set_source_kind(ring, GEODE_SOURCE_MIC);
    check(geode_ingest_source_kind(ring) == GEODE_SOURCE_MIC, "source kind: set, then read back");
    check(ringOf(ring).sourceKind() == geode::audio::ingest::kSourceMic, "source kind: the ring holds it");
    check(geode_ingest_source_kind(nullptr) == GEODE_SOURCE_PLAYER, "source kind: NULL reads as the player");
    geode_ingest_set_source_kind(nullptr, GEODE_SOURCE_MIC);   // no crash
    geode_ingest_destroy(ring);
}

}  // namespace

int main() {
    check(geode_ingest_create(0, 2) == nullptr && geode_ingest_create(1024, 0) == nullptr, "bad sizes give NULL");
    everySampleOnceInOrderWithItsRate();
    blockStopsAtAFormatBoundary();
    epochRestartsAndSetFormatDoesNot();
    overrunSkipsToOldestSafe();
    concurrentWriterNeverTears();
    conversions();
    downmixWeights();
    monoCentred();
    rawMonoTail();
    restingAfterTheHold();
    restingIsOneAnswerForManyPollers();
    levelFollowsTheRing();
    recordRingOverrun();
    sourceSwitchRestartsWithoutAWrongRateBurst();
    sourceKind();
    if (failures) {
        std::printf("%d ingest check(s) failed\n", failures);
        return 1;
    }
    std::printf("ingest: all checks passed\n");
    return 0;
}
