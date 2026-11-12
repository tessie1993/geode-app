// Host checks for a finished render's loudness (core/output/LoudnessScan on a fake decoder) and the sentence and
// numbers the shell words it with (core/output/LoudnessAdvice readingOf). Built and run by CI
// (.github/workflows/native-host-tests.yml, "Loudness scan host test"); no device.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "audio/dsp/Loudness.hpp"
#include "audio/dsp/PcmConvert.hpp"
#include "output/LoudnessAdvice.hpp"
#include "output/LoudnessScan.hpp"

using namespace geode::output::loudness;
using geode::audio::LoudnessAnalyser;
using geode::audio::LoudnessReport;
using geode::audio::ingest::DecodedChunk;
using geode::audio::ingest::DecodePort;

namespace {

int failures = 0;
constexpr double kPi = 3.14159265358979323846;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Buffer {
    std::vector<uint8_t> bytes;
    bool isFloat = false;
    int channels = 2;
    int rate = 48'000;
};

// Hands its buffers out one per takeOutput, the last flagged end of stream, after `silentTurns` empty dequeues.
class FakePort final : public DecodePort {
public:
    explicit FakePort(std::vector<Buffer> buffers) : buffers_(std::move(buffers)) {}

    int silentTurns = 0;
    bool throwOnOutput = false;
    bool neverOutput = false;
    int released = 0;

    bool feedInput() override { return ++fed_ >= 3; }
    bool takeOutput(DecodedChunk& out) override {
        if (throwOnOutput) throw std::runtime_error("codec error");
        if (neverOutput) return false;
        if (silentTurns > 0) {
            --silentTurns;
            return false;
        }
        if (next_ >= buffers_.size()) return false;
        const Buffer& b = buffers_[next_++];
        out.data = b.bytes.data();
        out.bytes = b.bytes.size();
        out.isFloat = b.isFloat;
        out.channels = b.channels;
        out.sampleRate = b.rate;
        out.endOfStream = next_ == buffers_.size();
        return true;
    }
    void releaseOutput() override { ++released; }

private:
    std::vector<Buffer> buffers_;
    size_t next_ = 0;
    int fed_ = 0;
};

// A 997 Hz tone at amplitude amp, interleaved over channels, frames long.
std::vector<float> tone(int frames, int channels, int rate, float amp) {
    std::vector<float> v(static_cast<size_t>(frames) * static_cast<size_t>(channels));
    for (int i = 0; i < frames; ++i) {
        const auto s = static_cast<float>(amp * std::sin(2.0 * kPi * 997.0 * i / rate));
        for (int c = 0; c < channels; ++c) v[static_cast<size_t>(i * channels + c)] = s;
    }
    return v;
}

std::vector<int16_t> shorts(const std::vector<float>& f) {
    std::vector<int16_t> s(f.size());
    geode::audio::floatToS16(f.data(), f.size(), s.data());
    return s;
}

template <typename T>
Buffer buffer(const T* samples, size_t count, bool isFloat, int channels = 2, int rate = 48'000) {
    Buffer b;
    b.bytes.resize(count * sizeof(T));
    std::memcpy(b.bytes.data(), samples, b.bytes.size());
    b.isFloat = isFloat;
    b.channels = channels;
    b.rate = rate;
    return b;
}

// The pcm split into buffers of `per` samples.
template <typename T>
std::vector<Buffer> split(const std::vector<T>& pcm, size_t per, bool isFloat, int channels = 2, int rate = 48'000) {
    std::vector<Buffer> out;
    for (size_t at = 0; at < pcm.size(); at += per) {
        out.push_back(buffer(pcm.data() + at, std::min(per, pcm.size() - at), isFloat, channels, rate));
    }
    return out;
}

bool sameReport(const LoudnessReport& a, const LoudnessReport& b) {
    return a.hasIntegrated == b.hasIntegrated && a.integratedLufs == b.integratedLufs &&
           a.truePeakDbtp == b.truePeakDbtp && a.samplePeakDbfs == b.samplePeakDbfs && a.measuredMs == b.measuredMs &&
           a.gatedBlockCount == b.gatedBlockCount;
}

void scans() {
    const auto pcm = tone(96'000, 2, 48'000, 0.25f);
    const auto s16 = shorts(pcm);

    // What the meter says fed the same samples at once, converted the same way.
    std::vector<float> asFloat(s16.size());
    geode::audio::s16ToFloat(s16.data(), s16.size(), asFloat.data());
    LoudnessAnalyser direct(48'000, 2);
    direct.feed(asFloat.data(), asFloat.size());
    const LoudnessReport expected = direct.finish();

    FakePort s16Port(split(s16, 2'048, false));
    s16Port.silentTurns = 5;
    const Scan a = scan(s16Port);
    check(a.end == ScanEnd::kMeasured, "two seconds of s16 are measured");
    check(sameReport(a.report, expected), "the s16 scan equals one feed of the same samples");
    check(s16Port.released == 94, "every output buffer is released");

    LoudnessAnalyser floats(48'000, 2);
    floats.feed(pcm.data(), pcm.size());
    FakePort floatPort(split(pcm, 9'000, true));
    const Scan b = scan(floatPort);
    check(b.end == ScanEnd::kMeasured && sameReport(b.report, floats.finish()), "a float scan equals one feed");

    const auto shortPcm = shorts(tone(9'600, 2, 48'000, 0.25f));
    FakePort tooShort(split(shortPcm, 4'096, false));
    check(scan(tooShort).end == ScanEnd::kTooShort, "a fifth of a second is too short");

    auto changing = split(s16, 2'048, false);
    changing[10].rate = 44'100;
    FakePort changed(changing);
    check(scan(changed).end == ScanEnd::kUnreadable, "a rate change part-way is unreadable");

    const auto three = shorts(tone(48'000, 3, 48'000, 0.25f));
    FakePort layout(split(three, 3'000, false, 3));
    check(scan(layout).end == ScanEnd::kUnreadable, "three channels have no BS.1770 weights");

    FakePort stalled(split(s16, 2'048, false));
    stalled.neverOutput = true;
    check(scan(stalled).end == ScanEnd::kUnreadable, "a decoder that never answers stalls");

    FakePort broken(split(s16, 2'048, false));
    broken.throwOnOutput = true;
    check(scan(broken).end == ScanEnd::kUnreadable, "a codec error is unreadable, not a throw");

    FakePort empty(std::vector<Buffer>{Buffer{}});
    check(scan(empty).end == ScanEnd::kUnreadable, "audio that decodes to nothing is unreadable");
}

void readings() {
    check(readingOf(kYouTube, false, 0.0, 0.0).sentence == kSayNothing, "silence");
    const Reading mixed = readingOf(kLeaveAsIs, true, -20.0, -0.5);
    check(mixed.sentence == kSayAsMixed && mixed.quietLu == 6.0 && mixed.peakWarning && mixed.ceilingDbtp == 0.0,
          "as mixed, quiet and over YouTube's ceiling");
    check(readingOf(kYouTube, true, -14.0, -3.0).sentence == kSayOnTarget, "on target");
    const Reading raise = readingOf(kYouTube, true, -20.0, -12.0);
    check(raise.sentence == kSayRaise && raise.gainDb == 6.0 && raise.resultingLufs == -14.0 &&
              raise.ceilingDbtp == -1.0,
          "raise to the level");
    const Reading limited = readingOf(kYouTube, true, -20.0, -6.0);
    check(limited.sentence == kSayRaiseLimited && limited.gainDb == 5.0 && limited.gainForLevelDb == 6.0,
          "the ceiling stops a raise short");
    check(readingOf(kYouTube, true, -20.0, 2.0).sentence == kSayLowerClipping, "quiet but clipping");
    check(readingOf(kYouTube, true, -12.0, 3.0).sentence == kSayLowerLoudLimited, "loud, held by the ceiling");
    check(readingOf(kYouTube, true, -8.0, -9.0).sentence == kSayLowerLoud, "louder than the platform");
    check(readingOf(kYouTube, true, -14.0, 0.5).sentence == kSayLowerPeak, "level right, peak over");
    check(readingOf(kShortsAndTikTok, true, -11.0, -3.0).sentence == kSayOnTarget, "on the short-form target");
}

}  // namespace

int main() {
    scans();
    readings();
    if (failures == 0) std::printf("loudness_scan_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
