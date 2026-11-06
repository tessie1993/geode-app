// Host checks for the playout clock (sound/interp/PlayoutClock, #349, #482): a live renderer must hear
// the player's audio when the speaker plays it, not when the sink takes it. Built and run by CI (see
// .github/workflows/android.yml, "Native host tests") with the system compiler; no device.
//
// The fake sink below behaves like Media3's DefaultAudioSink as the clock sees it. After a flush it
// fills its AudioTrack buffer at decode speed (each buffer it takes is an anchor: presentation time,
// ring position, frames), then plays, keeping that buffer topped up, and reports its position in media
// time every 10 ms. The position includes the output link's latency, as AudioTrack.getTimestamp does
// on Bluetooth, and counts speed and skipped silence in media time. The tap stays first in the chain,
// ahead of SilenceSkipping and Sonic (owner, #482), so the ring holds source samples 1:1 with media time.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void checkNear(int64_t got, int64_t want, int64_t tolerance, const char* what) {
    if (std::llabs(got - want) > tolerance) {
        std::printf("FAIL: %s (got %lld, want %lld +- %lld)\n", what, static_cast<long long>(got),
                    static_cast<long long>(want), static_cast<long long>(tolerance));
        ++failures;
    }
}

constexpr int64_t kMs = 1'000'000;  // ns per ms
constexpr int64_t kTickNs = 10 * kMs;  // how often the sink reports its position
constexpr int64_t kBufferFrames = 480;  // 10 ms at 48 kHz per buffer the sink takes

struct Sink {
    geode_playout* clock = geode_playout_create();
    int rate = 48'000;
    double speed = 1.0;
    int64_t trackMs = 250;  // the AudioTrack buffer, in wall time
    int64_t linkMs = 0;     // the output link past the AudioTrack (Bluetooth), in wall time
    int64_t startUs = 0;    // media time of this epoch's first frame
    int64_t ringBase = 0;   // ring position of this epoch's first frame
    int64_t handled = 0;    // media frames the sink took this epoch (all written to the ring)
    double headFrames = 0;  // media frames the AudioTrack consumed this epoch
    double heardFrames = 0;  // media frames the listener heard this epoch
    int64_t playedNs = 0;   // wall time played this epoch
    int64_t nowNs = 1'000 * kMs;
    int64_t nextTickNs = 1'000 * kMs + kTickNs;
    bool playing = false;

    Sink() = default;
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;
    ~Sink() { geode_playout_destroy(clock); }

    int64_t newest() const { return ringBase + handled; }
    int64_t ideal() const { return ringBase + static_cast<int64_t>(std::floor(heardFrames + 1e-6)); }
    int64_t frame() const { return geode_playout_speaker_frame(clock, nowNs); }

    // The sink keeps its AudioTrack buffer full: trackMs of wall time is trackMs * speed of media.
    void fill() {
        const double target = headFrames + static_cast<double>(trackMs) * rate * speed / 1000.0;
        while (static_cast<double>(handled) < target) {
            geode_playout_buffer_handled(clock, startUs + handled * 1'000'000 / rate, ringBase + handled,
                                         kBufferFrames);
            handled += kBufferFrames;
        }
    }

    void report() {
        const auto heard = static_cast<int64_t>(std::floor(heardFrames + 1e-6));
        geode_playout_position(clock, startUs + heard * 1'000'000 / rate, nowNs);
    }

    void step(int64_t ns) {
        if (playing) {
            const int64_t heardBefore = std::max<int64_t>(0, playedNs - linkMs * kMs);
            playedNs += ns;
            const int64_t heardAfter = std::max<int64_t>(0, playedNs - linkMs * kMs);
            headFrames += static_cast<double>(ns) * rate * speed / 1e9;
            heardFrames += static_cast<double>(heardAfter - heardBefore) * rate * speed / 1e9;
        }
        nowNs += ns;
        fill();
    }

    void advanceTo(int64_t t) {
        while (nowNs < t) {
            step(std::min(t, nextTickNs) - nowNs);
            if (nowNs == nextTickNs) {
                report();
                nextTickNs += kTickNs;
            }
        }
    }

    void advance(int64_t ms) { advanceTo(nowNs + ms * kMs); }

    // A first play or a seek: the sink flushes, takes its start burst at decode speed, then plays.
    // The ring keeps counting across the seek, so the new epoch starts where the old one stopped.
    void start(int64_t atUs) {
        geode_playout_flush(clock);
        startUs = atUs;
        ringBase += handled;
        handled = 0;
        headFrames = 0;
        heardFrames = 0;
        playedNs = 0;
        fill();
        report();
        playing = true;
        geode_playout_playing(clock, 1, nowNs);
    }

    void pause() {
        playing = false;
        geode_playout_playing(clock, 0, nowNs);
        report();
    }

    void resume() {
        playing = true;
        geode_playout_playing(clock, 1, nowNs);
    }

    void setSpeed(double s) {
        speed = s;
        geode_playout_speed(clock, static_cast<float>(s));
    }

    void offset(int ms) const { geode_playout_set_offset_ms(clock, ms); }

    // SilenceSkipping cuts `ms` of silence: the tap (first in the chain) already wrote those samples,
    // the AudioTrack never gets them, so the sink tops up past them and its media position jumps.
    void skipSilence(int64_t ms) {
        const double cut = static_cast<double>(ms) * rate / 1000.0;
        headFrames += cut;
        heardFrames += cut;
        fill();
        report();
    }
};

// Samples every 7 ms (off the 10 ms report grid) for `ms`, each within a frame of what is heard.
void tracks(Sink& s, int64_t ms, const char* what) {
    const int64_t end = s.nowNs + ms * kMs;
    while (s.nowNs + 7 * kMs <= end) {
        s.advance(7);
        checkNear(s.frame(), s.ideal(), 1, what);
        check(s.frame() <= s.newest(), "never a sample the ring does not hold yet");
    }
    s.advanceTo(end);
}

void followsTheSpeakerAt1x() {
    Sink s;
    s.start(10'000'000);
    check(s.newest() == 12'000, "1x: the start burst fills the 250 ms AudioTrack buffer");
    check(s.frame() == 0, "1x: none of the start burst is heard before it plays");
    tracks(s, 2'000, "1x: the speaker's sample, between reports too");
    // Before the clock the engine heard up to newest(), the whole buffer early.
    checkNear(s.newest() - s.frame(), 12'000, kBufferFrames, "1x: the engine trails the tap by the AudioTrack buffer");
}

void followsTheSpeakerAt1_5x() {
    Sink s;
    s.setSpeed(1.5);
    s.start(0);
    const int64_t t0 = s.nowNs;
    tracks(s, 1'000, "1.5x: the speaker's sample");
    s.advanceTo(t0 + 1'000 * kMs);
    checkNear(s.frame(), 72'000, 1, "1.5x: one wall second plays 1.5 media seconds");
    // The ring holds source samples (the tap is ahead of Sonic): 500 ms of wall time is 36 000 of them.
    const int64_t before = s.frame();
    s.advance(500);
    checkNear(s.frame() - before, 36'000, 1, "1.5x: the ring position runs 1.5x faster than wall time");
    s.setSpeed(1.0);  // on a report tick, as Media3 applies parameters between buffers
    tracks(s, 1'000, "1.5x then 1x: follows the new speed");
}

void bluetoothLatencyIsHeldBack() {
    Sink s;
    s.linkMs = 250;
    s.start(0);
    s.advance(200);
    check(s.frame() == 0, "Bluetooth: nothing is heard while the link fills");
    s.advance(1'800);
    tracks(s, 1'000, "Bluetooth: the sample the headset plays");
    checkNear(s.newest() - s.frame(), (250 + 250) * 48, kBufferFrames,
              "Bluetooth: the AudioTrack buffer and the 250 ms link are both held back");
}

void pauseHoldsAndResumeDoesNotJump() {
    Sink s;
    s.start(0);
    s.advance(500);
    const int64_t atPause = s.frame();
    s.pause();
    check(s.frame() == atPause, "pause: the sample stands");
    s.advance(3'000);
    check(s.frame() == atPause, "paused: still standing three seconds later");
    s.resume();
    check(s.frame() == atPause, "resume: the pause is not counted as play");
    s.advance(10);
    checkNear(s.frame(), atPause + 480, 1, "resumed: moves on at the play rate");
    tracks(s, 500, "resumed: the speaker's sample");
}

void skippedSilenceJumpsAhead() {
    Sink s;
    s.start(0);
    s.advance(1'000);
    const int64_t before = s.frame();
    s.skipSilence(300);  // on a report tick
    checkNear(s.frame() - before, 300 * 48, 1, "skip silence: the speaker jumps the 300 ms cut");
    check(s.frame() <= s.newest(), "skip silence: still never past the newest sample");
    tracks(s, 500, "skip silence: then follows the speaker again");
    checkNear(s.frame(), (1'000 + 300 + 500) * 48, 1, "skip silence: 1.8 s of media in 1.5 s of wall time");
}

void flushStartsANewMap() {
    Sink s;
    s.start(0);
    s.advance(800);
    geode_playout_flush(s.clock);
    check(s.frame() == -1, "flush: no map until the sink takes a buffer");
    const int64_t base = s.newest();
    geode_playout_buffer_handled(s.clock, 30'000'000, base, kBufferFrames);
    check(s.frame() == base, "seek: anchored but not positioned, nothing of it is heard yet");
    s.start(30'000'000);  // the sink's own flush, burst and first position at the new time
    check(s.ringBase == base && s.frame() == base, "seek: the speaker is at the new epoch's first sample");
    s.advance(500);
    checkNear(s.frame() - base, 24'000, 1, "seek: 500 ms after it, its first 500 ms are heard");
    tracks(s, 500, "seek: the speaker's sample");
}

void offsetShiftsThePicture() {
    {
        Sink s;
        s.start(0);
        s.advance(2'000);
        const int64_t f0 = s.frame();
        s.offset(100);
        checkNear(s.frame(), f0 - 4'800, 1, "offset +100 ms: the picture 100 ms later");
        s.offset(-50);
        checkNear(s.frame(), f0 + 2'400, 1, "offset -50 ms: the picture 50 ms earlier");
        s.offset(-1'000);
        check(s.frame() == s.newest(), "offset -1000 ms: never past the newest sample written");
        s.offset(5'000);
        checkNear(s.frame(), f0 - 48'000, 1, "offset is clamped to 1000 ms");
        s.offset(0);
        check(s.frame() == f0, "offset 0: the speaker's sample again");

        // Wall time, so a pause reaches the picture `offset` later and a resume likewise.
        s.offset(100);
        s.pause();
        const int64_t paused = s.ideal();
        checkNear(s.frame(), paused - 4'800, 1, "offset over a pause: still 100 ms behind at the pause");
        s.advance(50);
        checkNear(s.frame(), paused - 2'400, 1, "offset over a pause: plays on 50 ms after it");
        s.advance(50);
        check(s.frame() == paused, "offset over a pause: holds once the offset has passed");
        s.advance(1'000);
        check(s.frame() == paused, "offset over a pause: holds while paused");
        s.resume();
        s.advance(50);
        check(s.frame() == paused, "offset over a resume: holds for the offset");
        s.advance(100);
        checkNear(s.frame(), paused + 2'400, 1, "offset over a resume: then moves on");
    }
    {
        Sink s;
        s.setSpeed(1.5);
        s.start(0);
        s.advance(2'000);
        const int64_t f0 = s.frame();
        s.offset(100);
        checkNear(s.frame(), f0 - 7'200, 1, "offset at 1.5x: 100 ms of wall time is 150 ms of media");
    }
}

// The same script sampled at three frame rates: at every time all three share (each 100 ms), the
// sample is the same. Reads change nothing, so the frame rate cannot.
void sameTimeSameSampleAt30_60_120fps() {
    std::map<int64_t, int64_t> seen[3];
    const int rates[3] = {30, 60, 120};
    for (int r = 0; r < 3; ++r) {
        Sink s;
        s.linkMs = 250;
        s.setSpeed(1.5);
        s.offset(40);
        s.start(5'000'000);
        const int64_t t0 = s.nowNs;
        bool paused = false;
        bool resumed = false;
        bool slowed = false;
        int64_t last = s.frame();
        for (int64_t k = 0;; ++k) {
            const int64_t t = t0 + k * 1'000'000'000LL / rates[r];
            if (t > t0 + 3'500 * kMs) break;
            if (!paused && t >= t0 + 1'200 * kMs) {
                s.advanceTo(t0 + 1'200 * kMs);
                s.pause();
                paused = true;
            }
            if (!resumed && t >= t0 + 1'700 * kMs) {
                s.advanceTo(t0 + 1'700 * kMs);
                s.resume();
                resumed = true;
            }
            if (!slowed && t >= t0 + 2'300 * kMs) {
                s.advanceTo(t0 + 2'300 * kMs);
                s.setSpeed(1.0);
                slowed = true;
            }
            s.advanceTo(t);
            const int64_t v = s.frame();
            check(v <= s.newest(), "fps: never a sample the ring does not hold yet");
            check(v + 1 >= last, "fps: the speaker never runs backwards");
            last = std::max(last, v);
            if ((t - t0) % (100 * kMs) == 0) seen[r][t - t0] = v;
        }
    }
    check(seen[0].size() == 36, "fps: 36 shared sample times over 3.5 s");
    for (const auto& [t, v] : seen[0]) {
        check(seen[1].count(t) == 1 && seen[1][t] == v, "fps: 60 fps reads what 30 fps reads at the same time");
        check(seen[2].count(t) == 1 && seen[2][t] == v, "fps: 120 fps reads what 30 fps reads at the same time");
    }
    check(seen[0][3'500 * kMs] > seen[0][1'000 * kMs], "fps: the script actually played");
}

void longPlayOutlivesTheAnchorWindow() {
    // 20 s of 10 ms buffers is about 2 000 anchors, twice the clock's window: the oldest are dropped.
    Sink s;
    s.start(0);
    s.advance(20'000);
    tracks(s, 500, "20 s in: the speaker's sample");
    s.offset(1'000);
    checkNear(s.frame(), s.ideal() - 48'000, 1, "20 s in: a full second of offset is still inside the window");
}

void retriesDiscontinuitiesAndNull() {
    geode_playout* c = geode_playout_create();
    check(geode_playout_speaker_frame(c, 0) == -1, "new clock: no map");
    geode_playout_buffer_handled(c, 0, 0, 480);
    geode_playout_buffer_handled(c, 0, 0, 480);  // the sink retries a buffer the AudioTrack did not take whole
    geode_playout_buffer_handled(c, 10'000, 480, 480);
    geode_playout_position(c, 5'000, 0);
    check(geode_playout_speaker_frame(c, 0) == 240, "a retried buffer is one anchor; 5 ms in is sample 240");
    check(geode_playout_speaker_frame(c, 3'000 * kMs) == 240, "paused (never started): the position stands");
    geode_playout_buffer_handled(c, 2'000'000, 100, 480);  // the ring went back without a flush
    check(geode_playout_speaker_frame(c, 0) == 100, "a backward jump starts the map again from that anchor");
    geode_playout_destroy(c);

    geode_playout_buffer_handled(nullptr, 0, 0, 480);
    geode_playout_position(nullptr, 0, 0);
    geode_playout_playing(nullptr, 1, 0);
    geode_playout_flush(nullptr);
    geode_playout_speed(nullptr, 2.0f);
    geode_playout_set_offset_ms(nullptr, 10);
    check(geode_playout_speaker_frame(nullptr, 0) == -1, "a NULL clock has no map");
    geode_playout_destroy(nullptr);
}

}  // namespace

int main() {
    followsTheSpeakerAt1x();
    followsTheSpeakerAt1_5x();
    bluetoothLatencyIsHeldBack();
    pauseHoldsAndResumeDoesNotJump();
    skippedSilenceJumpsAhead();
    flushStartsANewMap();
    offsetShiftsThePicture();
    sameTimeSameSampleAt30_60_120fps();
    longPlayOutlivesTheAnchorWindow();
    retriesDiscontinuitiesAndNull();
    if (failures) {
        std::printf("%d playout check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("playout: all checks passed\n");
    return EXIT_SUCCESS;
}
