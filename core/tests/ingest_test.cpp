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
// - the ring's silence answer rests after the hold, and a ring nobody writes rests too.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <random>
#include <thread>
#include <utility>
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

void restingAfterTheHold() {
    geode_ingest* ing = geode_ingest_create(4096, 2);
    geode_ingest_begin_epoch(ing, 48000, 1, 0);
    const float loud[64] = {0.5f};
    const float quiet[64] = {};
    check(geode_ingest_resting(ing, 0) == 0, "the first poll counts as sound");
    geode_ingest_write(ing, loud, 64, GEODE_PCM_F32);
    check(geode_ingest_resting(ing, 1000) == 0, "audible: awake");
    check(geode_ingest_resting(ing, 6900) == 0, "within the hold: awake");
    check(geode_ingest_resting(ing, 7001) == 1, "after the hold: may rest");
    geode_ingest_write(ing, quiet, 64, GEODE_PCM_F32);
    check(geode_ingest_resting(ing, 8000) == 1, "digital silence written: still resting");
    geode_ingest_write(ing, loud, 64, GEODE_PCM_F32);
    check(geode_ingest_resting(ing, 8100) == 0, "sound wakes it at once");
    check(geode_ingest_resting(ing, 14100) == 0, "no writes, within the hold");
    check(geode_ingest_resting(ing, 14101) == 1, "no writes for the hold: silent");

    geode_ingest* idle = geode_ingest_create(1024, 2);
    check(geode_ingest_resting(idle, 5000) == 0 && geode_ingest_resting(idle, 11001) == 1,
          "a ring nobody writes rests after the hold");
    geode_ingest_destroy(idle);
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

// ---------------------------------------------------------------------------------------------------------
// Review cases for #549. Each one is red against the named mutant of the PR's code and green on the PR.
// Seams: the C ABI in core/api/geode_api.h (writer, written, resting), Ring's public header (capacity,
// runway, oldestSafe, records, record), Cursor's public header (read, skipToNewest, behind, rawMonoTail).
// Expected values are hand-worked literals or the headers' stated rules, never the code's own sums.

// The rate in force at each position, as the test's writer set it.
struct RateLog {
    std::vector<std::pair<int64_t, int>> changes;   // (first position, rate), ascending
    int at(int64_t p) const {
        int rate = 0;
        for (const auto& c : changes) {
            if (c.first <= p) rate = c.second;
        }
        return rate;
    }
};

int rate48k(int64_t) {
    return 48000;
}

void drain(Checker& r, int maxFrames, int (*rateAt)(int64_t)) {
    while (r.take(maxFrames, rateAt).frames > 0) {
    }
}

void writeMono16(geode_ingest* ing, int64_t from, int frames) {
    std::vector<int16_t> pcm(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) pcm[static_cast<size_t>(i)] = left(from + i);
    geode_ingest_write(ing, pcm.data(), static_cast<size_t>(frames), GEODE_PCM_S16);
}

void writeFloats(geode_ingest* ing, std::initializer_list<float> interleaved, int frames) {
    const std::vector<float> pcm(interleaved);
    geode_ingest_write(ing, pcm.data(), static_cast<size_t>(frames), GEODE_PCM_F32);
}

// Kills: bit_ceil dropped from the capacity rounding (a 3000-frame ring masked by 2999); the write loop's
// source offset without `done` (a write longer than the runway repeats its first chunk); a cursor's wrap
// index off by one. Three readers at three paces across many wraps and four rate changes.
void everyReaderHearsEverySampleOnceAcrossTheWrapOfAnUnevenRing() {
    geode_ingest* ing = geode_ingest_create(3000, 2);
    Ring& ring = ringOf(ing);
    check(ring.capacity() == 4096, "a 3000-frame ring rounds up to 4096, the next power of two");
    geode_ingest_begin_epoch(ing, 44100, 2, 0);
    RateLog rates;
    rates.changes.push_back({0, 44100});
    auto rateAt = [&rates](int64_t p) { return rates.at(p); };
    const int edge = ring.capacity() - ring.runway();   // the most a reader may lag and lose nothing
    Checker fast(ring, 4096);
    Checker slow(ring, 4096);
    Checker lagging(ring, 4096);
    std::mt19937 rng(549);
    std::uniform_int_distribution<int> chunk(1, edge / 2);   // past the runway, so one write splits
    std::uniform_int_distribution<int> take(1, 4096);
    const int cycle[] = {44100, 48000, 96000, 32000};
    int64_t written = 0;
    int changes = 1;
    for (int it = 0; written < 300'000; ++it) {
        if (written >= 40'000 * changes) {
            const int rate = cycle[changes % 4];
            geode_ingest_set_format(ing, rate, 2, 0);
            rates.changes.push_back({written, rate});
            ++changes;
        }
        const int n = chunk(rng);
        writeStereo16(ing, written, n);
        written += n;
        while (fast.take(take(rng), rateAt).frames > 0) {
        }
        // Every second write: at most two chunks (the safe edge) behind, so it must lose nothing.
        if (it % 2 == 1) {
            while (slow.take(take(rng), rateAt).frames > 0) {
            }
        }
        // Every fifth write: often past the edge, so it drops but never misreads.
        if (it % 5 == 4) {
            while (lagging.take(take(rng), rateAt).frames > 0) {
            }
        }
    }
    while (slow.take(4096, rateAt).frames > 0) {
    }
    while (lagging.take(4096, rateAt).frames > 0) {
    }
    check(fast.ok && fast.frames == written && fast.dropped == 0, "uneven ring: a reader that keeps up hears all");
    check(slow.ok && slow.frames == written && slow.dropped == 0, "uneven ring: a reader at the edge hears all");
    check(lagging.ok && lagging.dropped > 0 && lagging.frames + lagging.dropped == written,
          "uneven ring: a lagging reader drops, then reads the right samples at the right rates");
    geode_ingest_destroy(ing);
}

// Kills: oldestSafe without the runway (`written - capacity`), or with its sign flipped (`- runway`). The
// header's rule: a reader more than capacity - runway behind skips; exactly that far behind it does not.
void aReaderAtTheSafeEdgeLosesNothingAndOneFramePastLosesOne() {
    for (int extra = 0; extra <= 1; ++extra) {
        geode_ingest* ing = geode_ingest_create(1024, 2);
        Ring& ring = ringOf(ing);
        geode_ingest_begin_epoch(ing, 48000, 2, 0);
        Checker r(ring, 2048);
        const int64_t target = ring.capacity() - ring.runway() + extra;
        int64_t written = 0;
        while (written < target) {
            const int n = static_cast<int>(std::min<int64_t>(100, target - written));
            writeStereo16(ing, written, n);
            written += n;
        }
        drain(r, 2048, rate48k);
        check(r.ok && r.dropped == extra && r.frames == target - extra,
              extra ? "one frame past the safe edge: exactly that frame is dropped"
                    : "exactly at the safe edge: nothing is dropped");
        geode_ingest_destroy(ing);
    }
}

// Kills: the write loop's source offset without `done` (every runway-sized chunk of one call would repeat
// the first); a chunk of `runway` frames without `frames - done` (written would overshoot the call).
void oneWriteLongerThanTheRingKeepsItsNewestFrames() {
    geode_ingest* ing = geode_ingest_create(1024, 2);
    Ring& ring = ringOf(ing);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    Checker r(ring, 2048);
    writeStereo16(ing, 0, 3077);   // three rings and five frames, in one call
    check(geode_ingest_written(ing) == 3077, "one long write: written counts exactly the frames handed in");
    drain(r, 2048, rate48k);
    check(r.ok && r.frames == ring.capacity() - ring.runway() && r.frames + r.dropped == 3077,
          "one long write: the newest safe frames are the ones written there");
    geode_ingest_destroy(ing);
}

// Kills: Start::Newest and Start::Oldest swapped; Oldest starting at 0 rather than the oldest safe frame.
void aNewestCursorHearsOnlyWhatFollowsAndAnOldestOneStartsAtTheOldestSafeFrame() {
    geode_ingest* ing = geode_ingest_create(1024, 2);
    Ring& ring = ringOf(ing);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    writeStereo16(ing, 0, 500);
    Cursor newest(ring, Cursor::Start::Newest);
    std::vector<float> out(2 * 2048);
    check(newest.position() == 500 && newest.behind() == 0 && newest.read(out.data(), 2048).frames == 0,
          "a Newest cursor owes nothing already written");
    writeStereo16(ing, 500, 10);
    Cursor::Block b = newest.read(out.data(), 2048);
    check(b.frames == 10 && b.position == 500 && b.restarted && frameMatches(out.data(), 500),
          "a Newest cursor's first block is what came after it");

    int64_t written = 510;
    while (written < 5000) {
        const int n = static_cast<int>(std::min<int64_t>(100, 5000 - written));
        writeStereo16(ing, written, n);
        written += n;
    }
    Cursor oldest(ring, Cursor::Start::Oldest);
    b = oldest.read(out.data(), 2048);
    const int64_t held = ring.capacity() - ring.runway();
    check(b.dropped == 0 && b.frames == held && b.position == 5000 - held && frameMatches(out.data(), b.position),
          "an Oldest cursor on a lapped ring starts at the oldest safe frame, with nothing to drop");
    geode_ingest_destroy(ing);
}

// Kills: skipToNewest without its outer max (keeping more than is unread would move the cursor back and
// read frames twice) and without max(keep, 0) (a negative keep would skip frames not written yet).
void skipToNewestNeverRereadsAndNeverSkipsTheFuture() {
    geode_ingest* ing = geode_ingest_create(4096, 2);
    Ring& ring = ringOf(ing);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    Cursor c(ring, Cursor::Start::Oldest);
    std::vector<float> out(2 * 4096);
    writeStereo16(ing, 0, 100);
    c.read(out.data(), 50);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);   // a seek between the cursor and the pen
    writeStereo16(ing, 100, 100);
    check(c.skipToNewest(10) == 140, "skip across a seek: all but the newest ten unread frames");
    Cursor::Block b = c.read(out.data(), 4096);
    check(b.frames == 10 && b.position == 190 && b.dropped == 140 && b.restarted && frameMatches(out.data(), 190),
          "after skipping across a seek the reader still restarts, and reports the skip as dropped");

    writeStereo16(ing, 200, 20);
    check(c.skipToNewest(1000) == 0, "keeping more than is unread skips nothing");
    b = c.read(out.data(), 4096);
    check(b.frames == 20 && b.position == 200 && b.dropped == 0 && frameMatches(out.data(), 200),
          "keeping more than is unread never moves the cursor back");

    writeStereo16(ing, 220, 10);
    check(c.skipToNewest(-5) == 10, "a negative keep skips exactly what is unread");
    writeStereo16(ing, 230, 10);
    b = c.read(out.data(), 4096);
    check(b.frames == 10 && b.position == 230 && b.dropped == 10 && frameMatches(out.data(), 230),
          "a negative keep never skips frames written later");
    geode_ingest_destroy(ing);
}

// Kills: `maxFrames <= 0` as `< 0` (a read with no room would count as the first block, so the real one
// would lose restarted) and a read into a null buffer that still moves the cursor.
void aReadWithNoRoomTakesNothing() {
    geode_ingest* ing = geode_ingest_create(1024, 2);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    writeStereo16(ing, 0, 10);
    float out[2 * 16];
    check(c.read(out, 0).frames == 0 && c.read(nullptr, 10).frames == 0 && c.position() == 0,
          "a read with no room takes nothing");
    const Cursor::Block b = c.read(out, 16);
    check(b.frames == 10 && b.position == 0 && b.restarted, "the first real block still restarts the reader");
    geode_ingest_destroy(ing);
}

// Kills: wrong entries in the default layouts for three, four, five and seven channels; any of the
// downmix's speaker cases moved (front of centre, the tops, bottoms and the second LFE); the stereo fast
// path taken on two channels alone without checking they are front left and right; a mask that names the
// wrong number of speakers used anyway (popcount >= channels).
void everyLayoutDownmixesByBs775() {
    constexpr float h = 0.5f;
    const float three[3][2] = {{1, 0}, {0, 1}, {kC, kC}};
    checkWeights(3, 0, three, "3.0 with no mask: FL FR FC");
    const float quad[4][2] = {{1, 0}, {0, 1}, {kC, 0}, {0, kC}};
    checkWeights(4, 0, quad, "quad with no mask: FL FR BL BR");
    const float five[5][2] = {{1, 0}, {0, 1}, {kC, kC}, {kC, 0}, {0, kC}};
    checkWeights(5, 0, five, "5.0 with no mask: FL FR FC BL BR");
    checkWeights(5, 0xFC, five, "a mask naming six speakers on five channels: the default 5.0");
    const float sixOne[7][2] = {{1, 0}, {0, 1}, {kC, kC}, {kLfe, kLfe}, {kC, 0}, {0, kC}, {h, h}};
    checkWeights(7, 0, sixOne, "6.1 with no mask: the back centre at -6 dB to both sides");
    const float fiveOneSide[6][2] = {{1, 0}, {0, 1}, {kC, kC}, {kLfe, kLfe}, {kC, 0}, {0, kC}};
    checkWeights(6, 0x183C, fiveOneSide, "5.1 with side surrounds: sides at -3 dB");
    // Front left and right of centre, top front left and right.
    const float wide[4][2] = {{1, 0}, {0, 1}, {kC, 0}, {0, kC}};
    checkWeights(4, 0x14300, wide, "front of centre at unity to its side, top fronts at -3 dB");
    // Top centre, top front centre, top back centre, second LFE.
    const float tops[4][2] = {{h, h}, {h, h}, {h, h}, {kLfe, kLfe}};
    checkWeights(4, 0x204A000, tops, "top centres at -6 dB to both sides, the second LFE at -10 dB");
    // Top back L R, top side L R, bottom front L C R.
    const float lower[7][2] = {{kC, 0}, {0, kC}, {kC, 0}, {0, kC}, {kC, 0}, {h, h}, {0, kC}};
    checkWeights(7, 0x1FA0000, lower, "top backs, top sides and bottom fronts to their sides at -3 dB");
    // Two channels that are not a stereo pair: front centre and LFE.
    const float centreLfe[2][2] = {{kC, kC}, {kLfe, kLfe}};
    checkWeights(2, 0x30, centreLfe, "two channels laid out FC LFE are not passed through as L R");
    // Two channels with a mask that names five speakers: the mask is wrong, the default stereo holds.
    const float stereo[2][2] = {{1, 0}, {0, 1}};
    checkWeights(2, 0x1F0, stereo, "a mask naming five speakers on two channels: plain stereo");
}

// Kills: the kept-channel mask taking `keep + 1` speakers (firstSpeakers' `<=`), and the write loop
// stepping the source by the kept channels rather than the source's (the second frame would be misread).
void aSourceWiderThanTheRingKeepsItsFirstSpeakers() {
    geode_ingest* ing = geode_ingest_create(1024, 3);
    geode_ingest_begin_epoch(ing, 48000, 6, 0xFC);   // 5.1 into a ring that keeps three channels
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    writeFloats(ing, {0.5f, -0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 0.125f, 0.75f, 0.0f, 0.0f, 0.0f, 0.0f}, 2);
    float out[4];
    const Cursor::Block b = c.read(out, 2);
    check(b.frames == 2 && b.sourceChannels == 6 && b.channelMask == 0x1C,
          "5.1 kept to three channels: FL FR FC, from six");
    check(near(out[0], 1.20710678f) && near(out[1], 0.45710678f) && out[2] == 0.125f && out[3] == 0.75f,
          "5.1 kept to three channels: the centre at -3 dB, the next frame read from the next six samples");
    geode_ingest_destroy(ing);
}

// Kills: setFormat's "same format" test without the channel count (12 channels after 10 would be read as
// 10), and the cursor's cached downmix gains reused when only one of channels or mask matches (`||`).
// Past eight channels there is no mask: the first two are front left and right, the rest centres (-3 dB).
void aChannelCountChangePastEightIsReadInTheNewLayout() {
    geode_ingest* ing = geode_ingest_create(1024, 24);
    geode_ingest_begin_epoch(ing, 48000, 10, 0);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    std::vector<float> ten(10, 0.0f);
    ten[0] = 0.25f;
    ten[1] = -0.5f;
    ten[9] = 0.2f;
    geode_ingest_write(ing, ten.data(), 1, GEODE_PCM_F32);
    geode_ingest_set_format(ing, 48000, 12, 0);
    std::vector<float> twelve(24, 0.0f);
    twelve[0] = 0.125f;
    twelve[1] = 0.75f;
    twelve[11] = 0.4f;
    twelve[12] = -0.125f;
    twelve[13] = 0.0625f;
    twelve[22] = -0.2f;
    geode_ingest_write(ing, twelve.data(), 2, GEODE_PCM_F32);
    float out[4];
    Cursor::Block b = c.read(out, 2);
    check(b.frames == 1 && b.sourceChannels == 10 && b.channelMask == 0 && near(out[0], 0.39142136f) &&
              near(out[1], -0.35857864f),
          "ten channels, no mask: FL FR, then centres at -3 dB to both sides");
    b = c.read(out, 2);
    check(b.frames == 2 && b.sourceChannels == 12 && !b.restarted && near(out[0], 0.40784271f) &&
              near(out[1], 1.03284271f) && near(out[2], -0.26642136f) && near(out[3], -0.07892136f),
          "then twelve channels in the same epoch: every channel weighed in the new layout");
    geode_ingest_destroy(ing);
}

// Kills: the "same format" test without the mask, and cached gains reused while the channel count alone
// matches. Same rate, same six channels; channel 4 is a back left first, then a back centre.
void aMaskChangeAloneChangesTheDownmix() {
    geode_ingest* ing = geode_ingest_create(1024, 8);
    geode_ingest_begin_epoch(ing, 48000, 6, 0xFC);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    writeFloats(ing, {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f}, 1);
    geode_ingest_set_format(ing, 48000, 6, 0x243C);   // FL FR FC LFE BC TC
    writeFloats(ing, {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f}, 1);
    float out[4];
    const Cursor::Block first = c.read(out, 2);
    const bool backLeft = first.frames == 1 && near(out[0], kC) && out[1] == 0.0f;
    const Cursor::Block second = c.read(out, 2);
    check(backLeft && second.frames == 1 && second.channelMask == 0x243C && out[0] == 0.5f && out[1] == 0.5f,
          "the same channel count in a new layout: the back centre goes -6 dB to both sides");
    geode_ingest_destroy(ing);
}

// Kills: setFormat's unusable-format return without clearing the format (writes after a 0 Hz format would
// go on in the old one). The header: such a format drops writes until a usable one, and a new epoch with
// an unusable format still restarts readers on whatever comes next.
void anUnusableFormatDropsWritesUntilAUsableOne() {
    geode_ingest* ing = geode_ingest_create(1024, 2);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    float out[2 * 16];
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    writeStereo16(ing, 0, 5);
    Cursor::Block b = c.read(out, 16);
    const uint32_t first = b.epoch;

    geode_ingest_begin_epoch(ing, 0, 2, 0);
    writeStereo16(ing, 5, 5);
    geode_ingest_begin_epoch(ing, 48000, 0, 0);
    writeStereo16(ing, 5, 5);
    check(geode_ingest_written(ing) == 5, "a new epoch at 0 Hz or with no channels drops writes");
    geode_ingest_set_format(ing, 48000, 2, 0);
    writeStereo16(ing, 5, 5);
    b = c.read(out, 16);
    check(b.frames == 5 && b.position == 5 && b.restarted && b.epoch != first && frameMatches(out, 5),
          "the next usable format, the same as before the gap, still restarts the reader");

    geode_ingest_set_format(ing, 0, 2, 0);
    writeStereo16(ing, 10, 5);
    check(geode_ingest_written(ing) == 10, "a format change to 0 Hz drops writes too");
    geode_ingest_set_format(ing, 44100, 2, 0);
    writeStereo16(ing, 10, 5);
    b = c.read(out, 16);
    check(b.frames == 5 && b.position == 10 && !b.restarted && b.sampleRate == 44100 && frameMatches(out, 10),
          "a usable format after it resumes in the same epoch");
    geode_ingest_destroy(ing);
}

// Kills: record() without its sequence check (a reused slot would answer for the index it replaced). The
// header: record(i) holds for i in [records() - kRecords, records()) and is false outside it.
void formatRecordsAreHeldForTheLastKRecords() {
    geode_ingest* ing = geode_ingest_create(1 << 14, 2);
    Ring& ring = ringOf(ing);
    geode_ingest_begin_epoch(ing, 40000, 2, 0);
    writeStereo16(ing, 0, 3);
    for (int i = 1; i < 2 * Ring::kRecords + 3; ++i) {
        geode_ingest_set_format(ing, 40000 + i, 2, 0);
        writeStereo16(ing, 3 * i, 3);
    }
    const int64_t n = ring.records();
    geode::audio::ingest::Record r;
    check(!ring.record(n, r), "a record not published yet is not readable");
    check(!ring.record(n - Ring::kRecords - 1, r), "a record whose slot was reused is gone");
    bool ok = n == 2 * Ring::kRecords + 3;
    for (int64_t i = n - Ring::kRecords; i < n && ok; ++i) {
        ok = ring.record(i, r) && r.sampleRate == 40000 + i && r.start == 3 * i;
    }
    check(ok, "the last kRecords format records read back with their rate and first position");
    geode_ingest_destroy(ing);
}

// Kills: locate() taking the oldest record that covers a position instead of the newest, when two format
// changes land on one position with no samples between.
void twoFormatChangesAtOnePositionTheLastOneHolds() {
    geode_ingest* ing = geode_ingest_create(1024, 2);
    geode_ingest_begin_epoch(ing, 44100, 2, 0);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    writeStereo16(ing, 0, 10);
    geode_ingest_set_format(ing, 32000, 2, 0);
    geode_ingest_set_format(ing, 96000, 2, 0);
    writeStereo16(ing, 10, 10);
    float out[2 * 32];
    const Cursor::Block a = c.read(out, 32);
    const Cursor::Block b = c.read(out, 32);
    check(a.frames == 10 && a.sampleRate == 44100 && b.frames == 10 && b.position == 10 && b.sampleRate == 96000,
          "two format changes with no samples between: the samples carry the last one");
    geode_ingest_destroy(ing);
}

// Kills: the first-poll rule dropped (heard time starting at 0, so a surface polled first late in the
// clock would rest at once). The clock is injected through now_ms.
void aSurfaceNeverRestsTheInstantItStarts() {
    geode_ingest* idle = geode_ingest_create(1024, 2);
    check(geode_ingest_resting(idle, 100000) == 0, "first poll at t = 100 s: awake");
    check(geode_ingest_resting(idle, 106000) == 0, "6 s after the first poll: still awake");
    check(geode_ingest_resting(idle, 106001) == 1, "past 6 s after the first poll with nothing written: rests");
    geode_ingest_destroy(idle);
}

// Kills: the ring's audible test as `>=` (disagreeing with core/sound/Silence at the -42 dBFS edge), the
// peak without fabs (a negative sample unheard), and a peak over the first channel only.
// The oracle is Silence's own audible(): the ring must call the same samples sound.
void theRingHearsExactlyWhatTheSilenceRuleHears() {
    const float edge = geode::sound::silence::kPeak;
    const float above = std::nextafter(edge, 1.0f);
    const float* atEdge[] = {&edge};
    const float* overEdge[] = {&above};
    check(!geode::sound::silence::audible(atEdge, 1, 1) && geode::sound::silence::audible(overEdge, 1, 1),
          "oracle: Silence calls the edge silence and the next float sound");

    geode_ingest* ing = geode_ingest_create(1024, 2);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    check(geode_ingest_resting(ing, 0) == 0, "first poll: awake");
    writeFloats(ing, {edge, -edge}, 1);
    check(geode_ingest_resting(ing, 6001) == 1, "samples at the edge itself are silence: rests after 6 s");
    writeFloats(ing, {0.0f, -0.5f}, 1);
    check(geode_ingest_resting(ing, 6002) == 0, "a loud negative sample on the right channel wakes it");
    check(geode_ingest_resting(ing, 12002) == 0 && geode_ingest_resting(ing, 12003) == 1,
          "then rests 6 s after that poll");
    writeFloats(ing, {above, 0.0f}, 1);
    check(geode_ingest_resting(ing, 12004) == 0, "the next float above the edge wakes it");
    geode_ingest_destroy(ing);
}

// Kills: the heard time set to the poll's clock rather than moved forward only (a second poller whose
// clock reads earlier would pull it back and rest the surface early). The header: two pollers only ever
// move it forward.
void anEarlierPollNeverPullsTheHeardTimeBack() {
    geode_ingest* ing = geode_ingest_create(1024, 1);
    geode_ingest_begin_epoch(ing, 48000, 1, 0);
    check(geode_ingest_resting(ing, 0) == 0, "first poll");
    writeFloats(ing, {0.5f}, 1);
    check(geode_ingest_resting(ing, 10000) == 0, "heard at 10 s");
    writeFloats(ing, {0.5f}, 1);
    check(geode_ingest_resting(ing, 5000) == 0, "a poller whose clock reads 5 s hears it too");
    check(geode_ingest_resting(ing, 16000) == 0, "6 s after 10 s: still awake, the earlier poll moved nothing back");
    check(geode_ingest_resting(ing, 16001) == 1, "past 6 s after 10 s: rests");
    geode_ingest_destroy(ing);
}

// Kills: any null-handle guard dropped from geode_ingest_api.cpp, and Ring::write without its null-bytes
// guard (each would crash the test).
void aNullHandleOrBufferIsHarmless() {
    const int16_t pcm[4] = {1, 2, 3, 4};
    geode_ingest_begin_epoch(nullptr, 48000, 2, 0);
    geode_ingest_set_format(nullptr, 48000, 2, 0);
    geode_ingest_write(nullptr, pcm, 2, GEODE_PCM_S16);
    check(geode_ingest_write_bytes(nullptr, pcm, sizeof pcm, GEODE_PCM_S16) == 0 &&
              geode_ingest_written(nullptr) == 0 && geode_ingest_resting(nullptr, 1LL << 40) == 0,
          "a null handle: nothing taken, nothing written, not resting");
    geode_ingest_destroy(nullptr);

    geode_ingest* ing = geode_ingest_create(1024, 2);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    geode_ingest_write(ing, nullptr, 10, GEODE_PCM_S16);
    check(geode_ingest_write_bytes(ing, nullptr, 40, GEODE_PCM_S16) == 0 && geode_ingest_written(ing) == 0,
          "a null buffer writes nothing");
    geode_ingest_destroy(ing);
}

// Kills: write_bytes sizing frames by the sample width alone (41 bytes of 5.1 s24 is 2 frames, not 13).
void writeBytesTakesWholeMultichannelFrames() {
    geode_ingest* fresh = geode_ingest_create(1024, 8);
    uint8_t bytes[256] = {};
    check(geode_ingest_write_bytes(fresh, bytes, 41, GEODE_PCM_S16) == 0, "write_bytes before any format: none");
    geode_ingest_destroy(fresh);

    geode_ingest* ing = geode_ingest_create(1024, 8);
    geode_ingest_begin_epoch(ing, 48000, 6, 0);   // 5.1, 18 bytes a frame in s24
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    bytes[2] = 0x40;        // frame 0 FL = 0.5
    bytes[18 + 2] = 0x20;   // frame 1 FL = 0.25
    bytes[18 + 5] = 0xC0;   // frame 1 FR = -0.5
    check(geode_ingest_write_bytes(ing, bytes, 41, 99) == 0, "write_bytes in an unknown encoding: none");
    check(geode_ingest_write_bytes(ing, bytes, 41, GEODE_PCM_S24_PACKED) == 2 && geode_ingest_written(ing) == 2,
          "41 bytes of 5.1 s24: two whole frames");
    float out[4];
    const Cursor::Block b = c.read(out, 2);
    check(b.frames == 2 && out[0] == 0.5f && out[1] == 0.0f && out[2] == 0.25f && out[3] == -0.5f,
          "write_bytes: each frame read from its own 18 bytes");
    geode_ingest_destroy(ing);
}

// Kills: 32-bit and 24-bit samples assembled without their low byte, 16-bit without its low byte, and the
// float guard as isnan or isnormal (negative infinity let through, a denormal zeroed). Hand-worked values.
void lowBitsSurviveConversion() {
    geode_ingest* ing = geode_ingest_create(1024, 1);
    geode_ingest_begin_epoch(ing, 48000, 1, 0);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    float l = 0.0f;
    float r = 0.0f;

    const int16_t s16[] = {1, 255, -2};
    const float s16Want[] = {3.0517578125e-05f, 0.007781982421875f, -6.103515625e-05f};
    geode_ingest_write(ing, s16, 3, GEODE_PCM_S16);
    bool ok = true;
    for (float want : s16Want) {
        readOne(c, l, r);
        ok = ok && l == want;
    }
    check(ok, "int16: the low byte counts (1, 255 and -2 LSB)");

    const uint8_t u8[] = {1};
    geode_ingest_write(ing, u8, 1, GEODE_PCM_U8);
    readOne(c, l, r);
    check(l == -0.9921875f, "u8: 1 is -127/128");

    const uint8_t s24[] = {0x01, 0x00, 0x00, 0x56, 0x34, 0x12, 0xFF, 0xFF, 0x7F};
    const float s24Want[] = {1.1920928955078125e-07f, 0.14222216606140137f, 0.99999988079071044921875f};
    geode_ingest_write(ing, s24, 3, GEODE_PCM_S24_PACKED);
    ok = true;
    for (float want : s24Want) {
        readOne(c, l, r);
        ok = ok && l == want;
    }
    check(ok, "24-bit packed: 1 LSB, 0x123456 and full scale, every byte counts");

    const int32_t s32[] = {1, -1, 0x12345678, std::numeric_limits<int32_t>::max()};
    geode_ingest_write(ing, s32, 4, GEODE_PCM_S32);
    float got[4];
    for (float& g : got) {
        readOne(c, l, r);
        g = l;
    }
    // 0x12345678 / 2^31 = 0.1422222219..., whose nearest float is 0.14222222566604614.
    check(got[0] == 4.656612873077393e-10f && got[1] == -4.656612873077393e-10f && got[2] == 0.14222222566604614f &&
              got[3] == 1.0f,
          "32-bit: 1 LSB is 2^-31, 0x12345678 keeps its low byte, full scale reads 1");

    const float f32[] = {-std::numeric_limits<float>::infinity(), 1e-40f};
    geode_ingest_write(ing, f32, 2, GEODE_PCM_F32);
    readOne(c, l, r);
    const float negInf = l;
    readOne(c, l, r);
    check(negInf == 0.0f && l == 1e-40f, "float: negative infinity to silence, a denormal as written");
    geode_ingest_destroy(ing);
}

// Kills: oldestSafe without its runway (the raw tail, like a read, would hand out frames the pen may be
// over).
void theRawTailStopsAtTheOldestSafeFrame() {
    geode_ingest* ing = geode_ingest_create(1024, 2);
    Ring& ring = ringOf(ing);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    Cursor c(ring, Cursor::Start::Newest);
    for (int64_t w = 0; w < 3000; w += 100) writeStereo16(ing, w, 100);
    std::vector<float> out(2 * 2048);
    while (c.read(out.data(), 2048).frames > 0) {
    }
    std::vector<float> tail(1024);
    const int n = c.rawMonoTail(tail.data(), 1024);
    bool ok = n == ring.capacity() - ring.runway();
    for (int i = 0; i < n && ok; ++i) {
        const int64_t p = 3000 - n + i;
        ok = near(tail[static_cast<size_t>(i)], 0.5f * (left(p) / 32768.0f + right(p) / 32768.0f), 1e-7f);
    }
    check(ok, "after an overrun the raw tail holds only the safe frames, ending at the cursor");
    geode_ingest_destroy(ing);
}

// Kills: rawMonoTail without its skip of records past the cursor (an unread seek after the cursor would
// end the tail at once, though the cursor is still in the old epoch).
void anUnreadSeekDoesNotCutTheRawTail() {
    geode_ingest* ing = geode_ingest_create(4096, 2);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    writeStereo16(ing, 0, 300);
    std::vector<float> out(2 * 200);
    c.read(out.data(), 200);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    writeStereo16(ing, 300, 50);
    float tail[100];
    const int n = c.rawMonoTail(tail, 100);
    const float last = 0.5f * (left(199) / 32768.0f + right(199) / 32768.0f);
    check(n == 100 && near(tail[99], last, 1e-7f), "a seek written but unread leaves the tail where the cursor is");
    geode_ingest_destroy(ing);
}

// Kills: a raw-tail segment downmixed with another segment's gains, or placed at the wrong offset. Mono and
// stereo alternate inside one epoch; a mono frame's tail is the sample itself, a stereo one (L + R) / 2.
void theRawTailWeighsEachFormatInItsOwnLayout() {
    geode_ingest* ing = geode_ingest_create(4096, 2);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    Cursor c(ringOf(ing), Cursor::Start::Newest);
    writeStereo16(ing, 0, 30);
    geode_ingest_set_format(ing, 48000, 1, 0);
    writeMono16(ing, 30, 30);
    geode_ingest_set_format(ing, 48000, 2, 0);
    writeStereo16(ing, 60, 30);
    geode_ingest_set_format(ing, 48000, 1, 0);
    writeMono16(ing, 90, 30);
    std::vector<float> out(2 * 256);
    while (c.read(out.data(), 256).frames > 0) {
    }
    float tail[200];
    const int n = c.rawMonoTail(tail, 200);
    bool ok = n == 120;
    for (int i = 0; i < n && ok; ++i) {
        const bool mono = (i / 30) % 2 == 1;
        const float want = mono ? left(i) / 32768.0f : 0.5f * (left(i) / 32768.0f + right(i) / 32768.0f);
        ok = near(tail[i], want, 1e-7f);
    }
    check(ok, "the raw tail across mono and stereo stretches of one epoch");
    geode_ingest_destroy(ing);
}

// Probabilistic, the one place threads are the point. Kills: write() publishing once per call instead of
// once per runway-sized chunk (a call longer than the runway can overwrite frames a reader is copying
// while the write position still says they are safe). Calls here run up to three runways long.
void aRealWriterWithLongCallsNeverTears() {
    geode_ingest* ing = geode_ingest_create(1 << 13, 2);
    Ring& ring = ringOf(ing);
    geode_ingest_begin_epoch(ing, 48000, 2, 0);
    constexpr int64_t kTotal = 2'000'000;
    const int longest = 3 * ring.runway();
    std::atomic<bool> done{false};
    std::thread writer([&] {
        std::mt19937 rng(549);
        std::uniform_int_distribution<int> chunk(1, longest);
        int64_t w = 0;
        while (w < kTotal) {
            const int n = static_cast<int>(std::min<int64_t>(chunk(rng), kTotal - w));
            writeStereo16(ing, w, n);
            w += n;
        }
        done.store(true, std::memory_order_release);
    });
    Checker r(ring, 512);
    std::mt19937 rng(5490);
    std::uniform_int_distribution<int> take(1, 512);
    for (;;) {
        const bool finished = done.load(std::memory_order_acquire);
        const Cursor::Block b = r.take(take(rng), rate48k);
        if (finished && b.frames == 0) break;
    }
    writer.join();
    check(r.ok, "long writes: every frame read is the one written there, in order");
    check(r.frames + r.dropped == kTotal, "long writes: read plus dropped is everything written");
    geode_ingest_destroy(ing);
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
    recordRingOverrun();
    everyReaderHearsEverySampleOnceAcrossTheWrapOfAnUnevenRing();
    aReaderAtTheSafeEdgeLosesNothingAndOneFramePastLosesOne();
    oneWriteLongerThanTheRingKeepsItsNewestFrames();
    aNewestCursorHearsOnlyWhatFollowsAndAnOldestOneStartsAtTheOldestSafeFrame();
    skipToNewestNeverRereadsAndNeverSkipsTheFuture();
    aReadWithNoRoomTakesNothing();
    everyLayoutDownmixesByBs775();
    aSourceWiderThanTheRingKeepsItsFirstSpeakers();
    aChannelCountChangePastEightIsReadInTheNewLayout();
    aMaskChangeAloneChangesTheDownmix();
    anUnusableFormatDropsWritesUntilAUsableOne();
    formatRecordsAreHeldForTheLastKRecords();
    twoFormatChangesAtOnePositionTheLastOneHolds();
    aSurfaceNeverRestsTheInstantItStarts();
    theRingHearsExactlyWhatTheSilenceRuleHears();
    anEarlierPollNeverPullsTheHeardTimeBack();
    aNullHandleOrBufferIsHarmless();
    writeBytesTakesWholeMultichannelFrames();
    lowBitsSurviveConversion();
    theRawTailStopsAtTheOldestSafeFrame();
    anUnreadSeekDoesNotCutTheRawTail();
    theRawTailWeighsEachFormatInItsOwnLayout();
    aRealWriterWithLongCallsNeverTears();
    if (failures) {
        std::printf("%d ingest check(s) failed\n", failures);
        return 1;
    }
    std::printf("ingest: all checks passed\n");
    return 0;
}
