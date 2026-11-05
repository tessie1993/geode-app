// Host checks for the playout clock (audio/PlayoutClock, #349): a live renderer must hear the player's
// audio when the speaker plays it, not when the sink buffers it. Built and run by CI (see
// .github/workflows/android.yml, "Native export DSP tests") with the system compiler; no device.
//
// The fake sink below behaves like Media3's DefaultAudioSink as the clock sees it: after a flush it
// fills its AudioTrack buffer at decode speed (the tap writes those frames into the ring at once),
// then plays at the sample rate while reporting its position in media time.
#include <cstdio>
#include <cstdlib>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr long long kMs = 1'000'000;  // ns per ms

struct FakeSink {
    explicit FakeSink(geode_playout* c) : clock(c) {}

    geode_playout* clock;
    int epoch = 0;
    int rate = 48'000;
    long long startUs = 0;     // media time of the epoch's frame 0
    long long written = 0;     // ring frames the tap wrote this epoch
    long long bufferFrames = 0;
    long long playedFrames = 0;
    long long nowNs = 1'000 * kMs;

    // A seek or a new track: the tap flushes (new ring epoch), the sink fills its buffer at once.
    void start(long long atUs, int rateHz, long long bufferMs) {
        rate = rateHz;
        startUs = atUs;
        ++epoch;
        written = 0;
        playedFrames = 0;
        bufferFrames = bufferMs * rate / 1000;
        geode_playout_flush(clock);
        geode_playout_begin_epoch(clock, epoch, rate);
        geode_playout_buffer(clock, startUs);
        written = bufferFrames;  // the start burst, at decode speed
        geode_playout_position(clock, startUs, nowNs);
        geode_playout_playing(clock, 1, nowNs);
    }

    // Play `ms` of audio; the sink tops its buffer back up and reports its position every 10 ms.
    void play(long long ms) {
        for (long long t = 0; t < ms; t += 10) {
            nowNs += 10 * kMs;
            playedFrames += rate / 100;
            written = playedFrames + bufferFrames;
            geode_playout_position(clock, startUs + playedFrames * 1'000'000 / rate, nowNs);
        }
    }

    long long heard(long long laterNs = 0) const {
        return geode_playout_heard_frame(clock, epoch, written, nowNs + laterNs);
    }
};

void trailsTheTapByTheSinkBuffer() {
    FakeSink sink{geode_playout_create()};
    sink.start(10'000'000, 48'000, 250);
    check(sink.heard() == 0, "nothing is heard while the sink only filled its buffer");
    sink.play(1000);
    // Before the fix the engine heard up to `written`, 12 000 frames (250 ms) early.
    check(sink.written == 60'000, "the tap is 250 ms ahead after one second");
    check(sink.heard() == 48'000, "the engine hears what the speaker played: one second");
    // Between two reports the clock runs on: 5 ms later, 240 frames more.
    check(sink.heard(5 * kMs) == 48'240, "extrapolates between position reports");
    // Never past the tap, however long since the last report.
    check(sink.heard(10'000 * kMs) <= sink.written, "never hands out a frame not yet written");
    geode_playout_destroy(sink.clock);
}

void bluetoothLatencyIsCounted() {
    // A2DP: the sink's position (AudioTimestamp) trails a further 200 ms behind the AudioTrack's head.
    FakeSink sink{geode_playout_create()};
    sink.start(0, 48'000, 450);
    sink.play(2000);
    check(sink.written - sink.heard() == 450 * 48, "the whole 450 ms buffer plus link is held back");
    geode_playout_destroy(sink.clock);
}

void hiResStartBurstIsKept() {
    // 96 kHz with Media3's largest PCM buffer: a 72 000-frame start burst. The old cursor kept only its
    // newest 16 384 frames and dropped the head (often the downbeat). Now the first frames reach the
    // engine first: heard starts at 0 and grows at the play rate.
    FakeSink sink{geode_playout_create()};
    sink.start(0, 96'000, 750);
    check(sink.written == 72'000, "the burst fills the buffer");
    check(sink.heard() == 0, "none of the burst is heard before it plays");
    sink.play(100);
    check(sink.heard() == 9'600, "after 100 ms the engine has the first 100 ms, not the newest");
    geode_playout_destroy(sink.clock);
}

void pauseHoldsAndResumeDoesNotJump() {
    FakeSink sink{geode_playout_create()};
    sink.start(0, 48'000, 250);
    sink.play(500);
    const long long atPause = sink.heard();
    geode_playout_playing(sink.clock, 0, sink.nowNs);
    sink.nowNs += 3'000 * kMs;
    check(sink.heard() == atPause, "paused: the heard frame stands still");
    geode_playout_playing(sink.clock, 1, sink.nowNs);
    check(sink.heard() == atPause, "resumed: the pause is not counted as play");
    check(sink.heard(10 * kMs) == atPause + 480, "resumed: moves on at the play rate");
    geode_playout_destroy(sink.clock);
}

void speedScalesTheExtrapolation() {
    FakeSink sink{geode_playout_create()};
    sink.start(0, 48'000, 250);
    sink.play(100);
    geode_playout_speed(sink.clock, 2.0f);
    check(sink.heard(10 * kMs) == sink.heard() + 960, "at 2x the media runs twice as fast");
    geode_playout_destroy(sink.clock);
}

void liveSourcesAreHeardAsWritten() {
    // The mic or another app's audio begins its own ring epoch; no sink stands in front of it.
    FakeSink sink{geode_playout_create()};
    sink.start(0, 48'000, 250);
    sink.play(500);
    check(geode_playout_heard_frame(sink.clock, sink.epoch + 1, 4096, sink.nowNs) == 4096,
          "an epoch the player did not begin is heard as written");
    geode_playout_reset(sink.clock);
    check(geode_playout_heard_frame(sink.clock, sink.epoch, 4096, sink.nowNs) == 4096,
          "after a reset nothing is the player's");
    geode_playout_destroy(sink.clock);
}

void gaplessChangeWaitsForTheSpeaker() {
    // Track 2 starts at 180 s (renderer time is continuous). The tap begins its epoch when the sink
    // takes track 2's first buffer, while the speaker still plays track 1's last 250 ms.
    FakeSink sink{geode_playout_create()};
    sink.start(179'000'000, 48'000, 250);
    sink.play(750);  // speaker at 179.75 s; the tap has written up to 180.0 s
    geode_playout_begin_epoch(sink.clock, sink.epoch + 1, 48'000);
    geode_playout_buffer(sink.clock, 180'000'000);
    geode_playout_position(sink.clock, 179'750'000, sink.nowNs);
    check(geode_playout_heard_frame(sink.clock, sink.epoch + 1, 12'000, sink.nowNs) == 0,
          "the new track is not heard before the old one finishes");
    check(geode_playout_heard_frame(sink.clock, sink.epoch + 1, 12'000, sink.nowNs + 260 * kMs) == 0,
          "and the clock does not run past its extrapolation cap on its own");
    geode_playout_position(sink.clock, 180'100'000, sink.nowNs + 350 * kMs);
    check(geode_playout_heard_frame(sink.clock, sink.epoch + 1, 24'000, sink.nowNs + 350 * kMs) == 4'800,
          "100 ms into track 2, its first 100 ms are heard");
    geode_playout_destroy(sink.clock);
}

void untimedEpochHolds() {
    geode_playout* clock = geode_playout_create();
    geode_playout_begin_epoch(clock, 7, 48'000);
    check(geode_playout_heard_frame(clock, 7, 9'000, 0) == 0, "no first buffer yet: hold");
    geode_playout_buffer(clock, 0);
    check(geode_playout_heard_frame(clock, 7, 9'000, 0) == 0, "no position yet: hold");
    geode_playout_position(clock, 0, 0);
    geode_playout_flush(clock);
    check(geode_playout_heard_frame(clock, 7, 9'000, 0) == 0, "flushed: positions from before do not count");
    geode_playout_destroy(clock);
}

}  // namespace

int main() {
    trailsTheTapByTheSinkBuffer();
    bluetoothLatencyIsCounted();
    hiResStartBurstIsKept();
    pauseHoldsAndResumeDoesNotJump();
    speedScalesTheExtrapolation();
    liveSourcesAreHeardAsWritten();
    gaplessChangeWaitsForTheSpeaker();
    untimedEpochHolds();
    if (failures) {
        std::printf("%d playout check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("playout: all checks passed\n");
    return EXIT_SUCCESS;
}
