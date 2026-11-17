// The Java-object captures' C ABI (audio/ingest/CapturePump over audio/ingest/android/JavaCapture). Android
// only; marshalled by geode_ingest_jni.cpp.
#include "api/geode_capture_java_api.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include "audio/ingest/CapturePlan.hpp"
#include "audio/ingest/CapturePump.hpp"
#include "audio/ingest/android/JavaCapture.hpp"

namespace ingest = geode::audio::ingest;

struct geode_java_capture {
    std::unique_ptr<ingest::CapturePump> pump;
};

namespace {

constexpr int kFallbackRateHz = 48000;
constexpr int kFallbackCaptureRateMhz = 20000;
constexpr int kMilli = 1000;

int64_t nowMs() {
    using std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(steady_clock::now().time_since_epoch()).count();
}

geode_java_capture* run(std::unique_ptr<ingest::CaptureReader> reader, geode_pcm_ring* ring, int readBytes,
                        int encoding, int channels, int rateHz) {
    if (!reader) return nullptr;
    auto write = [ring, encoding, channels, rateHz](const void* data, size_t bytes) {
        float peak = 0.0f;
        geode_pcm_ring_write_bytes(ring, data, bytes, encoding, channels, rateHz, &peak);
        return peak;
    };
    auto pump = ingest::CapturePump::start(std::move(reader), write, readBytes, nowMs);
    if (!pump) return nullptr;
    return new geode_java_capture{std::move(pump)};
}

}  // namespace

extern "C" {

geode_java_capture* geode_java_capture_record(JNIEnv* env, jobject audio_record, geode_pcm_ring* ring, int rate_hz,
                                              int channels, int as_float) {
    const int sampleBytes = as_float ? 4 : 2;
    const int readBytes = ingest::kCaptureReadFrames * channels * sampleBytes;
    const bool valid = ring && rate_hz > 0 && channels > 0;
    auto reader = ingest::audioRecordReader(env, audio_record, valid ? readBytes : 0);
    return run(std::move(reader), ring, readBytes, as_float ? GEODE_PCM_FLOAT : GEODE_PCM_S16, channels, rate_hz);
}

geode_java_capture* geode_java_capture_visualizer(JNIEnv* env, jobject visualizer, geode_pcm_ring* ring,
                                                  int capture_size, int sampling_rate_mhz, int capture_rate_mhz) {
    if (!ring || capture_size <= 0) return nullptr;
    const int size = std::min(capture_size, geode_pcm_ring_max_write_frames(ring));
    const int rateHz = sampling_rate_mhz / kMilli > 0 ? sampling_rate_mhz / kMilli : kFallbackRateHz;
    const int periodMs = kMilli * kMilli / (capture_rate_mhz > 0 ? capture_rate_mhz : kFallbackCaptureRateMhz);
    auto reader = ingest::visualizerReader(env, visualizer, capture_size, size, periodMs);
    return run(std::move(reader), ring, size, GEODE_PCM_U8, 1, rateHz);
}

void geode_java_capture_stop(geode_java_capture* capture) {
    std::unique_ptr<geode_java_capture> owned(capture);
    if (owned && owned->pump) owned->pump->stop();
}

int geode_java_capture_active(const geode_java_capture* capture) {
    return capture && capture->pump->active() ? 1 : 0;
}

int geode_java_capture_quiet(const geode_java_capture* capture) {
    return capture && capture->pump->quiet() ? 1 : 0;
}

}  // extern "C"
