#include <memory>

#include "api/geode_api.h"
#include "audio/ingest/Cursor.hpp"
#include "audio/ingest/Ring.hpp"

namespace {
// The C enum is android.media.AudioFormat's ENCODING_*, and so is the ring's: one table, checked here.
static_assert(static_cast<int>(GEODE_PCM_S16) == static_cast<int>(geode::audio::ingest::kPcm16));
static_assert(static_cast<int>(GEODE_PCM_U8) == static_cast<int>(geode::audio::ingest::kPcm8));
static_assert(static_cast<int>(GEODE_PCM_F32) == static_cast<int>(geode::audio::ingest::kPcmFloat));
static_assert(static_cast<int>(GEODE_PCM_S24_PACKED) == static_cast<int>(geode::audio::ingest::kPcm24Packed));
static_assert(static_cast<int>(GEODE_PCM_S32) == static_cast<int>(geode::audio::ingest::kPcm32));
static_assert(static_cast<int>(GEODE_SOURCE_PLAYER) == static_cast<int>(geode::audio::ingest::kSourcePlayer));
static_assert(static_cast<int>(GEODE_SOURCE_MIC) == static_cast<int>(geode::audio::ingest::kSourceMic));
static_assert(static_cast<int>(GEODE_SOURCE_CAPTURE) == static_cast<int>(geode::audio::ingest::kSourceCapture));
static_assert(static_cast<int>(GEODE_SOURCE_OUTPUT_MIX) == static_cast<int>(geode::audio::ingest::kSourceOutputMix));
static_assert(static_cast<int>(GEODE_SOURCE_FILE) == static_cast<int>(geode::audio::ingest::kSourceFile));
}  // namespace

extern "C" {

geode_ingest* geode_ingest_create(int capacity_frames, int max_channels) {
    if (capacity_frames <= 0 || max_channels <= 0) return nullptr;
    return std::make_unique<geode_ingest>(capacity_frames, max_channels).release();
}

void geode_ingest_destroy(geode_ingest* ingest) {
    std::unique_ptr<geode_ingest> owned(ingest);
}

void geode_ingest_begin_epoch(geode_ingest* ingest, int sample_rate, int channels, int channel_mask) {
    if (ingest) ingest->ring.beginEpoch(sample_rate, channels, channel_mask);
}

void geode_ingest_set_format(geode_ingest* ingest, int sample_rate, int channels, int channel_mask) {
    if (ingest) ingest->ring.setFormat(sample_rate, channels, channel_mask);
}

void geode_ingest_write(geode_ingest* ingest, const void* bytes, size_t frames, int encoding) {
    if (ingest) ingest->ring.write(bytes, frames, encoding);
}

size_t geode_ingest_write_bytes(geode_ingest* ingest, const void* bytes, size_t byte_count, int encoding) {
    return ingest ? ingest->ring.writeBytes(bytes, byte_count, encoding) : 0;
}

long long geode_ingest_written(const geode_ingest* ingest) {
    return ingest ? static_cast<long long>(ingest->ring.written()) : 0;
}

int geode_ingest_resting(geode_ingest* ingest, long long now_ms) {
    // No ring is nothing heard: a surface with no source rests like one whose source went quiet.
    return !ingest || ingest->ring.resting(now_ms) ? 1 : 0;
}

float geode_ingest_level(geode_ingest* ingest, long long now_ms) {
    if (!ingest) return 0.0f;
    using geode::audio::ingest::Cursor;
    const geode::audio::ingest::Ring& ring = ingest->ring;
    return ingest->level.poll(ring.written(), now_ms, [&ring](float* out, int n) {
        // A cursor made for this poll and moved to the newest n frames reads them through the one downmix,
        // then hands back their mono tail: the same samples projectM's tail is made of. No allocation; a
        // read stops at each format boundary, so a few reads cover the window.
        Cursor cursor(ring, Cursor::Start::Oldest);
        cursor.skipToNewest(n);
        float stereo[2 * 128];
        for (int reads = 0; reads < 64 && cursor.read(stereo, 128).frames > 0; ++reads) {
        }
        return cursor.rawMonoTail(out, n);
    });
}

void geode_ingest_set_source_kind(geode_ingest* ingest, int kind) {
    if (ingest) ingest->ring.setSourceKind(kind);
}

int geode_ingest_source_kind(const geode_ingest* ingest) {
    return ingest ? ingest->ring.sourceKind() : GEODE_SOURCE_PLAYER;
}

}  // extern "C"
