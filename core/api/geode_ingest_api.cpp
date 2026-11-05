#include <memory>

#include "api/geode_api.h"
#include "audio/ingest/Ring.hpp"

namespace {
// The C enum is android.media.AudioFormat's ENCODING_*, and so is the ring's: one table, checked here.
static_assert(static_cast<int>(GEODE_PCM_S16) == static_cast<int>(geode::audio::ingest::kPcm16));
static_assert(static_cast<int>(GEODE_PCM_U8) == static_cast<int>(geode::audio::ingest::kPcm8));
static_assert(static_cast<int>(GEODE_PCM_F32) == static_cast<int>(geode::audio::ingest::kPcmFloat));
static_assert(static_cast<int>(GEODE_PCM_S24_PACKED) == static_cast<int>(geode::audio::ingest::kPcm24Packed));
static_assert(static_cast<int>(GEODE_PCM_S32) == static_cast<int>(geode::audio::ingest::kPcm32));
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

int geode_ingest_flush(geode_ingest* ingest, int sample_rate, int channels, int channel_mask, int reapplied,
                       long long position_us, long long period_id) {
    if (!ingest) return 0;
    geode::audio::ingest::FlushFacts facts;
    facts.sampleRate = sample_rate;
    facts.channels = channels;
    facts.channelMask = channel_mask;
    facts.reapplied = reapplied != 0;
    facts.positionUs = position_us;
    facts.periodId = period_id;
    if (ingest->flushJudge.judge(facts, ingest->ring.written()) == geode::audio::ingest::FlushVerdict::BeginEpoch) {
        ingest->ring.beginEpoch(sample_rate, channels, channel_mask);
        return 1;
    }
    ingest->ring.setFormat(sample_rate, channels, channel_mask);
    return 0;
}

long long geode_ingest_written(const geode_ingest* ingest) {
    return ingest ? static_cast<long long>(ingest->ring.written()) : 0;
}

int geode_ingest_resting(geode_ingest* ingest, long long now_ms) {
    return ingest && ingest->ring.resting(now_ms) ? 1 : 0;
}

}  // extern "C"
