// Which of Media3's output configs the native playback output plays (audio/output/OutputChoice); the
// platform track plays every other. The rule was the Kotlin OboeAudioOutput's. Values are Android's and
// Media3's as the shell passes them: AudioFormat.CHANNEL_OUT_*, C.ENCODING_PCM_*, C.USAGE_*, C.INDEX_UNSET.
#include <cstdio>

#include "audio/output/OutputChoice.hpp"

namespace {

using geode::audio::output::channelsOfMask;
using geode::audio::output::OutputRequest;
using geode::audio::output::plays;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kPcm16 = 2;
constexpr int kPcmFloat = 4;
constexpr int kPcm24 = 21;
constexpr int kAc3 = 5;
constexpr int kMask51 = 0xFC;
constexpr int kUsageGame = 14;

OutputRequest plain() {
    OutputRequest r;
    r.usage = 1;
    r.channelMask = 0xC;
    r.encoding = kPcm16;
    r.sampleRateHz = 48000;
    return r;
}

void plainPcmMonoOrStereoPlays() {
    check(plays(plain()), "16-bit stereo media at 48 kHz plays");
    OutputRequest r = plain();
    r.channelMask = 0x4;
    r.encoding = kPcmFloat;
    r.sampleRateHz = 44100;
    check(plays(r), "float mono at 44.1 kHz plays");
}

void everyPlatformOnlyConfigGoesToTheTrack() {
    OutputRequest r = plain();
    r.offload = true;
    check(!plays(r), "offload is the track's");
    r = plain();
    r.tunneling = true;
    check(!plays(r), "tunneling is the track's");
    r = plain();
    r.offloadGapless = true;
    check(!plays(r), "offload gapless is the track's");
    r = plain();
    r.playbackParameters = true;
    check(!plays(r), "speed applied by the output is the track's");
    r = plain();
    r.virtualDeviceId = 3;
    check(!plays(r), "a virtual device is the track's");
    r = plain();
    r.usage = kUsageGame;
    check(!plays(r), "audio that is not media is the track's");
}

void otherFormatsGoToTheTrack() {
    OutputRequest r = plain();
    r.channelMask = kMask51;
    check(!plays(r), "5.1 is the track's");
    r = plain();
    r.encoding = kPcm24;
    check(!plays(r), "24-bit is the track's");
    r = plain();
    r.encoding = kAc3;
    check(!plays(r), "an encoded stream is the track's");
    r = plain();
    r.sampleRateHz = 0;
    check(!plays(r), "no rate is the track's");
}

void channelsCountTheMask() {
    check(channelsOfMask(0x4) == 1, "mono is one channel");
    check(channelsOfMask(0xC) == 2, "stereo is two");
    check(channelsOfMask(kMask51) == 6, "5.1 is six");
    check(channelsOfMask(0) == 0, "no mask is none");
}

}  // namespace

int main() {
    plainPcmMonoOrStereoPlays();
    everyPlatformOnlyConfigGoesToTheTrack();
    otherFormatsGoToTheTrack();
    channelsCountTheMask();
    if (failures != 0) return 1;
    std::printf("output_choice_test: ok\n");
    return 0;
}
