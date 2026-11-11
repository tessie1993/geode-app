#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"
#include "audio/dsp/PcmConvert.hpp"
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

}  // namespace

struct geode_pcm_cursor {
    explicit geode_pcm_cursor(std::shared_ptr<const PcmRing> shared, int max_frames)
        : ring(std::move(shared)), cursor(*ring, max_frames) {}

    std::shared_ptr<const PcmRing> ring;  // declared first: the cursor refers to it
    Cursor cursor;
};

extern "C" {

geode_pcm_ring* geode_pcm_ring_create(int capacity_frames, int channels, int max_write_frames) {
    if (!PcmRing::valid(capacity_frames, channels, max_write_frames)) return nullptr;
    return new geode_pcm_ring{std::make_shared<PcmRing>(capacity_frames, channels, max_write_frames)};
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

int64_t geode_pcm_ring_written(const geode_pcm_ring* r) {
    return r ? r->ring->writtenFrames() : 0;
}

long long geode_pcm_ring_heard_at(const geode_pcm_ring* r, int frames, long long now_ms, long long heard_at_ms) {
    if (!r || frames <= 0) return heard_at_ms;
    std::vector<float> latest(static_cast<size_t>(frames) * static_cast<size_t>(r->ring->channelCount()));
    if (!r->ring->snapshotLatest(latest.data(), frames)) return heard_at_ms;
    // Any sample above the rule's level counts, whichever channel it is in, so the interleaved block is
    // judged as one run of samples.
    const float* block = latest.data();
    return geode::sound::silence::heardAt(&block, 1, static_cast<int>(latest.size()), now_ms, heard_at_ms);
}

geode_pcm_cursor* geode_pcm_cursor_create(const geode_pcm_ring* r, int max_frames) {
    return r ? new geode_pcm_cursor(r->ring, max_frames) : nullptr;
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
    geode::audio::floatToS16(c->cursor.interleaved(),
                             static_cast<size_t>(take.frames) * static_cast<size_t>(c->cursor.channels()), out);
    return take.frames;
}

void geode_pcm_cursor_reset(geode_pcm_cursor* c) {
    if (c) c->cursor.reset();
}

}  // extern "C"
