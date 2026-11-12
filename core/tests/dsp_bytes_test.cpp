// The shell's raw block into the playback DSP (geode_dsp_process_bytes): native works out the whole frames from
// the encoding and channel count the Kotlin processor passes through, runs them exactly as the typed calls do,
// and answers the bytes to pass on. Also pins the numbers the shell hands across: Media3's encodings and the
// GeodeNative mirror of the output's event bits. Usage: dsp_bytes_test <path to GeodeNative.kt>.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

#include "api/geode_api.h"

// Android's AudioFormat.ENCODING_PCM_16BIT and ENCODING_PCM_FLOAT, which Media3's C.ENCODING_PCM_* reuse.
static_assert(GEODE_PCM_S16 == 2, "AudioFormat.ENCODING_PCM_16BIT");
static_assert(GEODE_PCM_FLOAT == 4, "AudioFormat.ENCODING_PCM_FLOAT");

namespace {

int failures = 0;

#define CHECK(cond, ...)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            std::printf("FAIL %s:%d ", __FILE__, __LINE__);          \
            std::printf(__VA_ARGS__);                                \
            std::printf("\n");                                       \
            ++failures;                                              \
        }                                                            \
    } while (0)

geode_dsp* fresh(int channels) {
    geode_dsp* d = geode_dsp_create();
    geode_dsp_ramp_to(d, -6.0f, 0);
    geode_dsp_set_enabled(d, 1);
    geode_dsp_set_band(d, 2, 600);
    geode_dsp_set_limiter(d, 1);
    geode_dsp_configure(d, 48000, channels);
    geode_dsp_flush(d);
    return d;
}

template <typename T>
std::vector<T> signal(size_t samples, float phase) {
    std::vector<T> v(samples);
    for (size_t i = 0; i < samples; ++i) {
        const float s = 0.5f * std::sin(phase + 0.05f * static_cast<float>(i));
        if constexpr (std::is_same_v<T, float>) {
            v[i] = s;
        } else {
            v[i] = static_cast<int16_t>(s * 32767.0f);
        }
    }
    return v;
}

template <typename T>
constexpr int encodingOf() {
    return std::is_same_v<T, float> ? GEODE_PCM_FLOAT : GEODE_PCM_S16;
}

// What the shell did before: the typed call on the whole frames, the buffer's capacity alongside.
template <typename T>
void typed(geode_dsp* d, std::vector<T>& v, size_t frames, int channels) {
    if constexpr (std::is_same_v<T, float>) {
        geode_dsp_process(d, v.data(), v.size(), frames, channels);
    } else {
        geode_dsp_process_pcm16(d, v.data(), v.size(), frames, channels);
    }
}

template <typename T>
int bytesCall(geode_dsp* d, std::vector<T>& v, int bytes, int encoding, int channels) {
    return geode_dsp_process_bytes(d, v.data(), v.size() * sizeof(T), bytes, encoding, channels);
}

// Every part frame from none to one byte short, block after block through one chain, against the typed call.
template <typename T>
void wholeFramesRunAsTheTypedCall(int channels) {
    constexpr size_t kFrames = 300;
    const size_t frame = sizeof(T) * static_cast<size_t>(channels);
    geode_dsp* d = fresh(channels);
    geode_dsp* reference = fresh(channels);
    for (size_t extra = 0; extra < frame; ++extra) {
        auto v = signal<T>((kFrames + 1) * static_cast<size_t>(channels), 0.3f * static_cast<float>(extra));
        const auto original = v;
        auto r = v;
        const int kept = bytesCall(d, v, static_cast<int>(kFrames * frame + extra), encodingOf<T>(), channels);
        typed(reference, r, kFrames, channels);
        CHECK(kept == static_cast<int>(kFrames * frame), "enc %d ch %d extra %zu: kept %d", encodingOf<T>(), channels, extra, kept);
        CHECK(std::memcmp(v.data(), r.data(), kFrames * frame) == 0, "enc %d ch %d extra %zu: whole frames differ from the typed call",
              encodingOf<T>(), channels, extra);
        CHECK(std::memcmp(v.data(), original.data(), kFrames * frame) != 0, "enc %d ch %d extra %zu: nothing ran", encodingOf<T>(),
              channels, extra);
        CHECK(std::memcmp(v.data() + kFrames * static_cast<size_t>(channels), original.data() + kFrames * static_cast<size_t>(channels),
                          frame) == 0,
              "enc %d ch %d extra %zu: the part frame was touched", encodingOf<T>(), channels, extra);
    }
    geode_dsp_destroy(d);
    geode_dsp_destroy(reference);
}

// Nothing, or less than one frame: nothing to pass on, nothing touched.
template <typename T>
void noWholeFrame(int channels) {
    geode_dsp* d = fresh(channels);
    const int frame = static_cast<int>(sizeof(T)) * channels;
    auto v = signal<T>(static_cast<size_t>(channels) * 4, 0.2f);
    const auto original = v;
    for (int bytes = -frame; bytes < frame; ++bytes) {
        CHECK(bytesCall(d, v, bytes, encodingOf<T>(), channels) == 0, "enc %d ch %d: %d bytes keep none", encodingOf<T>(), channels, bytes);
    }
    CHECK(v == original, "enc %d ch %d: under one frame nothing is touched", encodingOf<T>(), channels);
    geode_dsp_destroy(d);
}

// A channel count the live chain does not run: whole frames of that count pass on, untouched.
template <typename T>
void otherChannelsPassThrough() {
    geode_dsp* d = fresh(2);
    for (int channels = 1; channels <= 8; channels += (channels == 1 ? 2 : 1)) {
        const size_t frame = sizeof(T) * static_cast<size_t>(channels);
        auto v = signal<T>(static_cast<size_t>(channels) * 101, 0.4f);
        const auto original = v;
        const int kept = bytesCall(d, v, static_cast<int>(frame * 100 + 1), encodingOf<T>(), channels);
        CHECK(kept == static_cast<int>(frame * 100), "enc %d ch %d on a stereo chain: kept %d", encodingOf<T>(), channels, kept);
        CHECK(v == original, "enc %d ch %d on a stereo chain is touched", encodingOf<T>(), channels);
    }
    geode_dsp_destroy(d);
}

// An encoding the chain does not know, or no channels, never reaches here; if it did it passes on whole.
void unknownFormatsPassOnWhole() {
    geode_dsp* d = fresh(2);
    auto v = signal<float>(64, 0.5f);
    const auto original = v;
    constexpr int kOther[] = {GEODE_PCM_U8, 21, 22, 0, -1};  // 8-bit, 24-bit, 32-bit (ENCODING_PCM_*), none
    for (int encoding : kOther) {
        CHECK(bytesCall(d, v, 255, encoding, 2) == 255, "encoding %d passes on all its bytes", encoding);
    }
    CHECK(bytesCall(d, v, 255, GEODE_PCM_FLOAT, 0) == 255, "no channels passes on all its bytes");
    CHECK(bytesCall(d, v, 255, GEODE_PCM_S16, -2) == 255, "negative channels pass on all their bytes");
    CHECK(v == original, "an unknown format is touched");
    geode_dsp_destroy(d);
}

// A null DSP (after release) or no buffer runs nothing and still answers the whole frames.
void nullAnswersWholeFrames() {
    auto v = signal<int16_t>(40, 0.6f);
    const auto original = v;
    CHECK(bytesCall(nullptr, v, 39, GEODE_PCM_S16, 2) == 36, "null dsp: 9 stereo 16-bit frames");
    CHECK(bytesCall(nullptr, v, 39, GEODE_PCM_FLOAT, 1) == 36, "null dsp: 9 mono float frames");
    CHECK(v == original, "a null dsp touches nothing");
    geode_dsp* d = fresh(2);
    CHECK(geode_dsp_process_bytes(d, nullptr, 0, 39, GEODE_PCM_S16, 2) == 36, "null pcm: 9 stereo 16-bit frames");
    geode_dsp_destroy(d);
}

// More bytes claimed than the buffer holds: held to its capacity, nothing past it touched.
void heldToCapacity() {
    constexpr float kGuard = 12345.0f;
    geode_dsp* d = fresh(2);
    std::vector<float> v(100 + 16, kGuard);
    for (size_t i = 0; i < 100; ++i) v[i] = 0.25f;
    const int kept = geode_dsp_process_bytes(d, v.data(), 99 * sizeof(float), 4000, GEODE_PCM_FLOAT, 2);
    CHECK(kept == 49 * 8, "held to capacity: kept %d", kept);
    bool rest = true;
    for (size_t i = 98; i < v.size(); ++i) rest = rest && v[i] == (i < 100 ? 0.25f : kGuard);
    CHECK(rest, "nothing past the whole frames that fit is touched");
    geode_dsp_destroy(d);
}

int constantIn(const std::string& kotlin, const std::string& name) {
    const std::string key = "const val " + name + " = ";
    const size_t at = kotlin.find(key);
    return at == std::string::npos ? -1 : std::atoi(kotlin.c_str() + at + key.size());
}

// GeodeNative mirrors the output's event bits for OboeAudioOutput; this keeps the mirror honest.
void geodeNativeMirrorsTheEventBits(const char* path) {
    std::ifstream in(path, std::ios::binary);
    const std::string kotlin((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(!kotlin.empty(), "cannot read %s", path);
    CHECK(constantIn(kotlin, "AUDIO_OUT_ADVANCING") == GEODE_AUDIO_OUT_ADVANCING, "AUDIO_OUT_ADVANCING differs from native");
    CHECK(constantIn(kotlin, "AUDIO_OUT_UNDERRUN") == GEODE_AUDIO_OUT_UNDERRUN, "AUDIO_OUT_UNDERRUN differs from native");
    CHECK(constantIn(kotlin, "AUDIO_OUT_FAILED") == GEODE_AUDIO_OUT_FAILED, "AUDIO_OUT_FAILED differs from native");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("usage: dsp_bytes_test <path to GeodeNative.kt>\n");
        return EXIT_FAILURE;
    }
    for (int channels : {1, 2}) {
        wholeFramesRunAsTheTypedCall<int16_t>(channels);
        wholeFramesRunAsTheTypedCall<float>(channels);
        noWholeFrame<int16_t>(channels);
        noWholeFrame<float>(channels);
    }
    otherChannelsPassThrough<int16_t>();
    otherChannelsPassThrough<float>();
    unknownFormatsPassOnWhole();
    nullAnswersWholeFrames();
    heldToCapacity();
    geodeNativeMirrorsTheEventBits(argv[1]);
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("dsp_bytes_test: all passed\n");
    return EXIT_SUCCESS;
}
