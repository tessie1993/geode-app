// Host test for core/audio/ingest/CapturePlan: the formats a capture tries and the buffer it asks for, pinned
// to the Kotlin ladders they replaced (MicCapture: device rate, then 48k, 44.1k, 22.05k, mono, float before
// 16-bit; PlaybackCapture: 48k, 44.1k, 22.05k, stereo before mono, float before 16-bit; both buffers
// max(min x 4, 1024 frames x channels x 4 bytes)), and the C ABI.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/capture_plan_test.cpp
//   core/audio/ingest/CapturePlan.cpp core/api/geode_capture_api.cpp -o capture_plan_test && ./capture_plan_test
#include <climits>
#include <cstdio>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/CapturePlan.hpp"

using geode::audio::ingest::captureBufferBytes;
using geode::audio::ingest::CaptureFormat;
using geode::audio::ingest::capturePlan;
using geode::audio::ingest::kMaxCaptureFormats;

static int failures = 0;
#define CHECK(cond, ...)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            std::fprintf(stderr, "FAIL %s:%d ", __FILE__, __LINE__); \
            std::fprintf(stderr, __VA_ARGS__);                       \
            std::fprintf(stderr, "\n");                              \
            ++failures;                                              \
        }                                                            \
    } while (0)

namespace {

struct Want {
    int rate;
    int channels;
    bool asFloat;
};

std::vector<CaptureFormat> plan(int deviceRateHz, bool stereo) {
    CaptureFormat out[kMaxCaptureFormats];
    const int count = capturePlan(deviceRateHz, stereo, out, kMaxCaptureFormats);
    return std::vector<CaptureFormat>(out, out + count);
}

void expectPlan(const char* name, const std::vector<CaptureFormat>& got, const std::vector<Want>& want) {
    CHECK(got.size() == want.size(), "%s: %zu formats, want %zu", name, got.size(), want.size());
    for (size_t i = 0; i < got.size() && i < want.size(); ++i) {
        CHECK(got[i].rateHz == want[i].rate && got[i].channels == want[i].channels &&
                  got[i].asFloat == want[i].asFloat,
              "%s[%zu]: %d Hz %d ch %s, want %d Hz %d ch %s", name, i, got[i].rateHz, got[i].channels,
              got[i].asFloat ? "float" : "s16", want[i].rate, want[i].channels, want[i].asFloat ? "float" : "s16");
    }
}

// MicCapture: the device rate first, then the fallbacks, mono, float before 16-bit.
void micWithDeviceRate() {
    expectPlan("mic 96k", plan(96000, false),
               {{96000, 1, true}, {96000, 1, false}, {48000, 1, true}, {48000, 1, false},
                {44100, 1, true}, {44100, 1, false}, {22050, 1, true}, {22050, 1, false}});
}

// A device rate that is a fallback is tried once, first (the Kotlin's distinct()).
void micDeviceRateIsAFallback() {
    expectPlan("mic 44.1k", plan(44100, false),
               {{44100, 1, true}, {44100, 1, false}, {48000, 1, true}, {48000, 1, false},
                {22050, 1, true}, {22050, 1, false}});
    expectPlan("mic 48k", plan(48000, false),
               {{48000, 1, true}, {48000, 1, false}, {44100, 1, true}, {44100, 1, false},
                {22050, 1, true}, {22050, 1, false}});
}

// No device rate (the property missing or not a number): the fallbacks alone.
void micUnknownRate() {
    const std::vector<Want> fallbacks = {{48000, 1, true}, {48000, 1, false}, {44100, 1, true},
                                         {44100, 1, false}, {22050, 1, true}, {22050, 1, false}};
    expectPlan("mic unknown", plan(0, false), fallbacks);
    expectPlan("mic negative", plan(-1, false), fallbacks);
}

// PlaybackCapture: no device rate, stereo before mono at each rate, float before 16-bit at each.
void playbackCapture() {
    expectPlan("playback", plan(0, true),
               {{48000, 2, true}, {48000, 2, false}, {48000, 1, true}, {48000, 1, false},
                {44100, 2, true}, {44100, 2, false}, {44100, 1, true}, {44100, 1, false},
                {22050, 2, true}, {22050, 2, false}, {22050, 1, true}, {22050, 1, false}});
    CHECK(static_cast<int>(plan(96000, true).size()) == kMaxCaptureFormats, "the most candidates fit");
}

void capacityCountsAll() {
    CaptureFormat two[2];
    CHECK(capturePlan(0, true, two, 2) == 12, "count is every candidate, whatever fits");
    CHECK(two[1].rateHz == 48000 && two[1].channels == 2 && !two[1].asFloat, "the first two written");
    CHECK(capturePlan(0, false, nullptr, 0) == 6, "a null out counts");
}

// max(min x 4, 1024 x channels x 4); a refused format (min <= 0) is 0.
void bufferBytes() {
    CHECK(captureBufferBytes(3840, 1) == 15360, "min x 4 above one read: %d", captureBufferBytes(3840, 1));
    CHECK(captureBufferBytes(512, 1) == 4096, "one mono read of floats: %d", captureBufferBytes(512, 1));
    CHECK(captureBufferBytes(512, 2) == 8192, "one stereo read of floats: %d", captureBufferBytes(512, 2));
    CHECK(captureBufferBytes(2048, 2) == 8192, "min x 4 equal to one read: %d", captureBufferBytes(2048, 2));
    CHECK(captureBufferBytes(0, 1) == 0, "zero: skipped");
    CHECK(captureBufferBytes(-2, 1) == 0, "ERROR_BAD_VALUE: skipped");
    CHECK(captureBufferBytes(-1, 2) == 0, "ERROR: skipped");
    CHECK(captureBufferBytes(4096, 0) == 0, "no channels: skipped");
    CHECK(captureBufferBytes(INT_MAX, 2) == INT_MAX, "no overflow: %d", captureBufferBytes(INT_MAX, 2));
}

void cAbi() {
    int rows[kMaxCaptureFormats * GEODE_CAPTURE_FORMAT_INTS] = {};
    const int count = geode_capture_plan(96000, 1, rows, kMaxCaptureFormats);
    CHECK(count == 16, "C ABI count %d", count);
    CHECK(rows[0] == 96000 && rows[1] == 2 && rows[2] == 1, "C ABI first row");
    CHECK(rows[3] == 96000 && rows[4] == 2 && rows[5] == 0, "C ABI second row");
    const int last = (count - 1) * GEODE_CAPTURE_FORMAT_INTS;
    CHECK(rows[last] == 22050 && rows[last + 1] == 1 && rows[last + 2] == 0, "C ABI last row");
    int one[GEODE_CAPTURE_FORMAT_INTS] = {};
    CHECK(geode_capture_plan(0, 0, one, 1) == 6, "C ABI counts past capacity");
    CHECK(one[0] == 48000 && one[1] == 1 && one[2] == 1, "C ABI one row");
    CHECK(geode_capture_buffer_bytes(3840, 1) == 15360, "C ABI buffer");
    CHECK(geode_capture_read_frames() == 1024, "C ABI read frames");
    // One read's bytes: frames x channels x the encoding's width; nothing for an encoding the ring cannot read.
    CHECK(geode_capture_read_bytes(4, 2) == 1024 * 2 * 4, "C ABI read bytes, float stereo");
    CHECK(geode_capture_read_bytes(2, 2) == 1024 * 2 * 2, "C ABI read bytes, 16-bit stereo");
    CHECK(geode_capture_read_bytes(2, 1) == 1024 * 2, "C ABI read bytes, 16-bit mono");
    CHECK(geode_capture_read_bytes(3, 1) == 1024, "C ABI read bytes, 8-bit mono");
    CHECK(geode_capture_read_bytes(99, 2) == 0 && geode_capture_read_bytes(4, 0) == 0, "C ABI read bytes, refused");
}

}  // namespace

int main() {
    micWithDeviceRate();
    micDeviceRateIsAFallback();
    micUnknownRate();
    playbackCapture();
    capacityCountsAll();
    bufferBytes();
    cAbi();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("capture_plan_test: ok\n");
    return 0;
}
