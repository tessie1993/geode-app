#include <memory>

#include "api/geode_api.h"
#include "audio/output/OboeOutput.hpp"
#include "audio/output/OutputFifo.hpp"
#include "audio/output/RetryBackoff.hpp"

// The playback output's C ABI (audio/output). Android only: OboeOutput includes Oboe.

using geode::audio::output::OboeOutput;
using geode::audio::output::OutputFifo;

struct geode_audio_out {
    std::shared_ptr<OboeOutput> output;
};

static_assert(GEODE_AUDIO_OUT_ADVANCING == geode::audio::output::kEventAdvancing);
static_assert(GEODE_AUDIO_OUT_UNDERRUN == geode::audio::output::kEventUnderrun);
static_assert(GEODE_AUDIO_OUT_FAILED == OboeOutput::kEventFailed);

namespace {
geode::audio::output::RetryBackoff gBackoff;
}

extern "C" {

void geode_audio_out_note_failed(int64_t now_ms) {
    gBackoff.noteFailed(now_ms);
}

int geode_audio_out_may_open(int64_t now_ms) {
    return gBackoff.mayOpen(now_ms) ? 1 : 0;
}

int geode_audio_out_supports(int encoding, int channels, int sample_rate) {
    return OutputFifo::supports(encoding, channels, sample_rate) ? 1 : 0;
}

geode_audio_out* geode_audio_out_open(int sample_rate, int channels, int encoding, int buffer_bytes, int session_id) {
    OboeOutput::Config config;
    config.sampleRateHz = sample_rate;
    config.channelCount = channels;
    config.encoding = encoding;
    config.bufferBytes = buffer_bytes;
    config.sessionId = session_id;
    std::shared_ptr<OboeOutput> output = OboeOutput::open(config);
    if (!output) return nullptr;
    return new geode_audio_out{std::move(output)};
}

void geode_audio_out_release(geode_audio_out* o) {
    std::unique_ptr<geode_audio_out> owned(o);
    if (owned) owned->output->release();
}

void geode_audio_out_play(geode_audio_out* o) {
    if (o) o->output->play();
}

void geode_audio_out_pause(geode_audio_out* o) {
    if (o) o->output->pause();
}

void geode_audio_out_flush(geode_audio_out* o) {
    if (o) o->output->flush();
}

void geode_audio_out_stop(geode_audio_out* o) {
    if (o) o->output->stop();
}

int64_t geode_audio_out_write(geode_audio_out* o, const void* data, size_t bytes) {
    if (!o || !data) return -1;
    return o->output->write(data, bytes);
}

int64_t geode_audio_out_position_us(geode_audio_out* o) {
    return o ? o->output->positionUs() : 0;
}

void geode_audio_out_set_volume(geode_audio_out* o, float volume) {
    if (o) o->output->setVolume(volume);
}

void geode_audio_out_set_device(geode_audio_out* o, int device_id) {
    if (o) o->output->setDevice(device_id);
}

int geode_audio_out_events(geode_audio_out* o) {
    return o ? o->output->events() : 0;
}

int geode_audio_out_stalled(geode_audio_out* o) {
    return o && o->output->stalled() ? 1 : 0;
}

int geode_audio_out_session_id(const geode_audio_out* o) {
    return o ? o->output->sessionId() : 0;
}

int64_t geode_audio_out_buffer_frames(const geode_audio_out* o) {
    return o ? o->output->bufferFrames() : 0;
}

}  // extern "C"
