#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/Cursor.hpp"
#include "audio/ingest/Ring.hpp"
#include "viz/SoundFeed.hpp"

// A shell-side reader of an ingest ring (#481): cast sends the player's music to the TV through Android's
// AAC encoder, so the samples have to reach Kotlin as int16. The reading (the cursor, the downmix, the
// catch-up after a stall) and the float -> int16 conversion stay here; the shell only resamples through
// the native PcmResampler and queues the block, as before. Cast used to read Kotlin's SampleRing through
// a SoundCursor and convert per sample in Kotlin; both are gone for the player.
struct geode_ingest_reader {
    explicit geode_ingest_reader(const geode::audio::ingest::Ring& ring)
        : cursor(ring, geode::audio::ingest::Cursor::Start::Newest) {}
    geode::audio::ingest::Cursor cursor;
    std::vector<float> stereo;
};

extern "C" {

geode_ingest_reader* geode_ingest_reader_create(geode_ingest* ingest) {
    if (!ingest) return nullptr;
    return std::make_unique<geode_ingest_reader>(ingest->ring).release();
}

void geode_ingest_reader_destroy(geode_ingest_reader* reader) {
    std::unique_ptr<geode_ingest_reader> owned(reader);
}

int geode_ingest_reader_read_s16(geode_ingest_reader* reader, int16_t* out, int max_frames, int* sample_rate) {
    if (sample_rate) *sample_rate = 0;
    if (!reader || !out || max_frames <= 0) return 0;
    // The same bound a renderer keeps: after a stall, what plays now, not seconds of backlog.
    reader->cursor.skipToNewest(geode::viz::SoundFeed::kMaxCatchUpFrames);
    if (reader->stereo.size() < static_cast<size_t>(max_frames) * 2) reader->stereo.resize(static_cast<size_t>(max_frames) * 2);
    const geode::audio::ingest::Cursor::Block b = reader->cursor.read(reader->stereo.data(), max_frames);
    if (b.frames <= 0) return 0;
    const size_t samples = static_cast<size_t>(b.frames) * 2;
    for (size_t i = 0; i < samples; ++i) {
        const float x = reader->stereo[i];   // a float source can carry NaN; never let it reach lround
        const float s = std::isfinite(x) ? std::clamp(x, -1.0f, 1.0f) : 0.0f;
        out[i] = static_cast<int16_t>(std::lround(s * 32767.0f));
    }
    if (sample_rate) *sample_rate = b.sampleRate;
    return b.frames;
}

}  // extern "C"
