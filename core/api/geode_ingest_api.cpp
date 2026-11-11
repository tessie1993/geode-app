#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"
#include "audio/ingest/PcmRing.hpp"
#include "sound/Silence.hpp"

namespace {

using geode::audio::ingest::Cursor;
using geode::audio::ingest::PcmRing;

static_assert(GEODE_PCM_S16 == geode::audio::ingest::kSigned16);
static_assert(GEODE_PCM_U8 == geode::audio::ingest::kUnsigned8);
static_assert(GEODE_PCM_FLOAT == geode::audio::ingest::kFloat32);

int report(const PcmRing::Written& written, float* peak_out) {
    if (peak_out) *peak_out = written.peak;
    return written.frames;
}

// Float to 16-bit for Cast's encoder: clamped, scaled by 32767 and rounded, as the Kotlin it replaces
// did (the export's floatToS16 truncates); NaN becomes 0.
int16_t roundToS16(float v) {
    if (std::isnan(v)) return 0;
    const float clamped = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<int16_t>(std::lrintf(clamped * 32767.0f));
}

}  // namespace

struct geode_pcm_cursor {
    explicit geode_pcm_cursor(std::shared_ptr<const PcmRing> shared, int max_frames)
        : ring(std::move(shared)), cursor(*ring, max_frames) {}

    std::shared_ptr<const PcmRing> ring;  // declared first: the cursor refers to it
    Cursor cursor;
};

// No exception may leave an extern "C" function: the three that allocate catch it and report nothing made
// or nothing heard. The rest neither allocate nor throw.
extern "C" {

geode_pcm_ring* geode_pcm_ring_create(int capacity_frames, int channels, int max_write_frames) {
    if (!PcmRing::valid(capacity_frames, channels, max_write_frames)) return nullptr;
    try {
        return new geode_pcm_ring{std::make_shared<PcmRing>(capacity_frames, channels, max_write_frames), nullptr};
    } catch (...) {
        return nullptr;
    }
}

void geode_pcm_ring_destroy(geode_pcm_ring* r) {
    std::unique_ptr<geode_pcm_ring> owned(r);
}

int geode_pcm_ring_write_f32(geode_pcm_ring* r, const float* interleaved, int frames, int channels, int sample_rate,
                             float* peak_out) {
    return report(r ? r->ring->writeFloat(interleaved, frames, channels, sample_rate) : PcmRing::Written{}, peak_out);
}

int geode_pcm_ring_write_s16(geode_pcm_ring* r, const int16_t* interleaved, int frames, int channels, int sample_rate,
                             float* peak_out) {
    return report(r ? r->ring->writeInt16(interleaved, frames, channels, sample_rate) : PcmRing::Written{}, peak_out);
}

int geode_pcm_ring_write_bytes(geode_pcm_ring* r, const void* data, size_t bytes, int encoding, int channels,
                               int sample_rate, float* peak_out) {
    return report(r ? r->ring->writeBytes(data, bytes, encoding, channels, sample_rate) : PcmRing::Written{},
                  peak_out);
}

void geode_pcm_ring_begin_epoch(geode_pcm_ring* r) {
    if (r) r->ring->beginEpoch();
}

int64_t geode_pcm_ring_head(const geode_pcm_ring* r) {
    return r ? r->ring->headFrames() : 0;
}

void geode_pcm_ring_set_playout(geode_pcm_ring* r, const geode_playout* clock) {
    if (r) r->playout.store(clock, std::memory_order_release);
}

int64_t geode_pcm_ring_written(const geode_pcm_ring* r) {
    return r ? r->ring->writtenFrames() : 0;
}

long long geode_pcm_ring_heard_at(const geode_pcm_ring* r, int frames, long long now_ms, long long heard_at_ms) {
    // Checked against the ring before anything is allocated for it.
    if (!r || frames <= 0 || frames > r->ring->capacityFrames()) return heard_at_ms;
    try {
        std::vector<float> latest(static_cast<size_t>(frames) * static_cast<size_t>(r->ring->channelCount()));
        if (!r->ring->snapshotLatest(latest.data(), frames)) return heard_at_ms;
        // Any sample above the rule's level counts, whichever channel it is in, so the interleaved block
        // is judged as one run of samples.
        const float* block = latest.data();
        return geode::sound::silence::heardAt(&block, 1, static_cast<int>(latest.size()), now_ms, heard_at_ms);
    } catch (...) {
        return heard_at_ms;
    }
}

geode_pcm_cursor* geode_pcm_cursor_create(const geode_pcm_ring* r, int max_frames) {
    if (!r) return nullptr;
    try {
        return new geode_pcm_cursor(r->ring, max_frames);
    } catch (...) {
        return nullptr;
    }
}

void geode_pcm_cursor_destroy(geode_pcm_cursor* c) {
    std::unique_ptr<geode_pcm_cursor> owned(c);
}

int geode_pcm_cursor_max_frames(const geode_pcm_cursor* c) {
    return c ? c->cursor.maxFrames() : 0;
}

int geode_pcm_cursor_channels(const geode_pcm_cursor* c) {
    return c ? c->cursor.channels() : 0;
}

int geode_pcm_cursor_take(geode_pcm_cursor* c, float* out, int capacity_frames, int* restarted, int* sample_rate) {
    if (!c || !out || capacity_frames < c->cursor.maxFrames()) return -1;
    const Cursor::Take take = c->cursor.take();
    if (restarted) *restarted = take.restarted ? 1 : 0;
    if (sample_rate) *sample_rate = take.sampleRateHz;
    const size_t samples = static_cast<size_t>(take.frames) * static_cast<size_t>(c->cursor.channels());
    std::copy(c->cursor.interleaved(), c->cursor.interleaved() + samples, out);
    return take.frames;
}

int geode_pcm_cursor_take_s16(geode_pcm_cursor* c, int16_t* out, int capacity_frames, int* restarted,
                              int* sample_rate) {
    if (!c || !out || capacity_frames < c->cursor.maxFrames()) return -1;
    const Cursor::Take take = c->cursor.take();
    if (restarted) *restarted = take.restarted ? 1 : 0;
    if (sample_rate) *sample_rate = take.sampleRateHz;
    const size_t samples = static_cast<size_t>(take.frames) * static_cast<size_t>(c->cursor.channels());
    const float* in = c->cursor.interleaved();
    for (size_t i = 0; i < samples; ++i) out[i] = roundToS16(in[i]);
    return take.frames;
}

void geode_pcm_cursor_reset(geode_pcm_cursor* c) {
    if (c) c->cursor.reset();
}

}  // extern "C"
