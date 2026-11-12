// A capture's format plan's C ABI (audio/ingest/CapturePlan.hpp). Pure, any thread; marshalled by
// geode_ingest_jni.cpp.
#include "api/geode_api.h"
#include "audio/ingest/CapturePlan.hpp"

namespace ingest = geode::audio::ingest;

extern "C" {

int geode_capture_plan(int device_rate_hz, int stereo, int* out, int capacity) {
    ingest::CaptureFormat formats[ingest::kMaxCaptureFormats];
    const int count = ingest::capturePlan(device_rate_hz, stereo != 0, formats, ingest::kMaxCaptureFormats);
    for (int i = 0; out && i < count && i < capacity && i < ingest::kMaxCaptureFormats; ++i) {
        out[i * GEODE_CAPTURE_FORMAT_INTS] = formats[i].rateHz;
        out[i * GEODE_CAPTURE_FORMAT_INTS + 1] = formats[i].channels;
        out[i * GEODE_CAPTURE_FORMAT_INTS + 2] = formats[i].asFloat ? 1 : 0;
    }
    return count;
}

int geode_capture_buffer_bytes(int min_buffer_bytes, int channels) {
    return ingest::captureBufferBytes(min_buffer_bytes, channels);
}

int geode_capture_read_frames(void) { return ingest::kCaptureReadFrames; }

}  // extern "C"
