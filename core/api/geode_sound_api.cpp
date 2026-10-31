#include <memory>

#include "api/geode_api.h"
#include "sound/SoundEngine.hpp"

static_assert(sizeof(GeodeSoundFrame) == GEODE_SOUND_FRAME_FLOATS * sizeof(float));
static_assert(sizeof(GeodeSoundTuning) == GEODE_SOUND_TUNING_FLOATS * sizeof(float));

struct geode_sound {
    geode::sound::SoundEngine engine;
};

extern "C" {

geode_sound* geode_sound_create(int sample_rate) {
    if (sample_rate <= 0) return nullptr;
    return std::make_unique<geode_sound>(geode_sound{geode::sound::SoundEngine(sample_rate)}).release();
}

void geode_sound_destroy(geode_sound* s) {
    std::unique_ptr<geode_sound> owned(s);
}

void geode_sound_write(geode_sound* s, const float* interleaved, size_t frames, int channels) {
    if (s && interleaved) s->engine.write(interleaved, static_cast<int>(frames), channels);
}

void geode_sound_step(geode_sound* s, float dt_seconds, GeodeSoundFrame* out) {
    if (s && out) s->engine.step(dt_seconds, *out);
}

void geode_sound_set_tuning(geode_sound* s, const GeodeSoundTuning* tuning) {
    if (s && tuning) s->engine.setTuning(*tuning);
}

void geode_sound_reset(geode_sound* s) {
    if (s) s->engine.reset();
}

int geode_sound_fft_size(geode_sound* s) {
    return s ? s->engine.fftSize() : 0;
}

}  // extern "C"
