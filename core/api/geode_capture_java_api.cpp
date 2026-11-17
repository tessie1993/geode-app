// The Java-object captures' C ABI (audio/ingest/CapturePump over audio/ingest/android/JavaCapture). Android
// only; marshalled by geode_ingest_jni.cpp.
#include "api/geode_capture_java_api.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include "audio/ingest/CapturePump.hpp"
#include "audio/ingest/android/JavaCapture.hpp"

namespace ingest = geode::audio::ingest;

struct geode_java_capture {
    std::unique_ptr<ingest::CapturePump> pump;
};

namespace {

int64_t nowMs() {
    using std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(steady_clock::now().time_since_epoch()).count();
}

geode_java_capture* run(std::unique_ptr<ingest::CaptureReader> reader, geode_pcm_ring* ring,
                        const ingest::JavaCaptureFormat& format) {
    if (!reader) return nullptr;
    auto write = [ring, format](const void* data, size_t bytes) {
        float peak = 0.0f;
        geode_pcm_ring_write_bytes(ring, data, bytes, format.encoding, format.channels, format.rateHz, &peak);
        return peak;
    };
    auto pump = ingest::CapturePump::start(std::move(reader), write, format.readBytes, nowMs);
    if (!pump) return nullptr;
    return new geode_java_capture{std::move(pump)};
}

}  // namespace

extern "C" {

geode_java_capture* geode_java_capture_playback(JNIEnv* env, jobject media_projection, geode_pcm_ring* ring) {
    if (!ring) return nullptr;
    ingest::JavaCaptureFormat format;
    auto reader = ingest::playbackCaptureReader(env, media_projection, &format);
    return run(std::move(reader), ring, format);
}

geode_java_capture* geode_java_capture_output_mix(JNIEnv* env, geode_pcm_ring* ring) {
    if (!ring) return nullptr;
    ingest::JavaCaptureFormat format;
    auto reader = ingest::outputMixReader(env, geode_pcm_ring_max_write_frames(ring), &format);
    if (!reader) return nullptr;
    // A new run is a new stream: the renderer's cursor restarts instead of reading a seam.
    geode_pcm_ring_begin_epoch(ring);
    return run(std::move(reader), ring, format);
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
