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
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
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
        check(geode_playout_max_offset_ms() == 1'000, "the bound Settings offers is the clamp");
        s.offset(geode_playout_max_offset_ms());
        checkNear(s.frame(), f0 - 48'000, 1, "offset at the offered bound: not clamped further");
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

// ---- Scripts fed straight through the C ABI, for what the fake sink cannot play: a stalled sink,
// retries, a gapless rate change, a sink clock that drifts, hours of uptime. Every expected value is
// worked by hand from 48 kHz (480 samples per 10 ms, 48 per ms) or 44.1 kHz (441 per 10 ms). Each case
// names the mutant of sound/interp/PlayoutClock.cpp it fails against.

// System.nanoTime on a phone left on for 30 days: far past what 32 bits hold.
constexpr int64_t kUptimeNs = 2'592'000'000'000'000;

struct Clock {
    geode_playout* c = geode_playout_create();
    Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    ~Clock() { geode_playout_destroy(c); }
    // `count` consecutive 10 ms buffers of `frames` samples from media time `fromUs`, ring `fromRing`.
    void buffers(int64_t fromUs, int64_t fromRing, int count, int64_t frames) const {
        for (int i = 0; i < count; ++i) {
            geode_playout_buffer_handled(c, fromUs + i * 10'000, fromRing + i * frames, frames);
        }
    }
    void handled(int64_t us, int64_t ring, int64_t frames) const { geode_playout_buffer_handled(c, us, ring, frames); }
    void position(int64_t us, int64_t ns) const { geode_playout_position(c, us, ns); }
    void playing(bool on, int64_t ns) const { geode_playout_playing(c, on ? 1 : 0, ns); }
    void offset(int ms) const { geode_playout_set_offset_ms(c, ms); }
    void speed(float s) const { geode_playout_speed(c, s); }
    int64_t at(int64_t ns) const { return geode_playout_speaker_frame(c, ns); }
};

// Kills :97 (the stall cap removed, or measured from the read time tNs instead of nowNs).
void aStalledSinkHoldsAQuarterSecondPastItsLastReport() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(0, 0, 400, 480);
    k.position(0, t);
    k.playing(true, t);
    check(k.at(t + 100 * kMs) == 4'800, "stall: 100 ms after the last report the speaker is 100 ms on");
    check(k.at(t + 250 * kMs) == 12'000, "stall: it runs on for a quarter second");
    check(k.at(t + 1'000 * kMs) == 12'000, "stall: then holds instead of running past what was played");
    check(k.at(t + 60'000 * kMs) == 12'000, "stall: still holding a minute later");
    k.offset(100);
    check(k.at(t + 1'000 * kMs) == 7'200, "stall: a +100 ms offset shows 100 ms before the hold");
    k.offset(0);
    k.playing(false, t + 1'000 * kMs);
    check(k.at(t + 2'000 * kMs) == 12'000, "stall then pause: the pause keeps the held sample");
    k.playing(true, t + 3'000 * kMs);
    check(k.at(t + 3'010 * kMs) == 12'480, "stall, pause, resume: 10 ms on from the held sample");
}

// Kills :57 (the pause does not count what played since the last report).
void aPauseBetweenReportsKeepsWhatPlayedSinceTheLastOne() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(0, 0, 100, 480);
    k.position(0, t);
    k.playing(true, t);
    k.playing(false, t + 5 * kMs);
    check(k.at(t + 5 * kMs) == 240, "pause 5 ms after a report: the speaker stopped 5 ms on");
    check(k.at(t + 2'000 * kMs) == 240, "pause 5 ms after a report: and stands there");
}

// Kills :50 (a repeated play or pause opens a new play span).
void aRepeatedPlayOrPauseChangesNothing() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(0, 0, 200, 480);
    k.position(0, t);
    k.playing(true, t);
    k.playing(true, t + 5 * kMs);
    check(k.at(t + 5 * kMs) == 240, "play again while playing: the 5 ms played still count");
    k.position(250'000, t + 250 * kMs);
    k.position(500'000, t + 500 * kMs);
    k.playing(false, t + 500 * kMs);
    k.playing(false, t + 2'000 * kMs);
    k.offset(100);
    check(k.at(t + 2'000 * kMs) == 24'000, "pause again while paused: the time between is not play");
}

// Kills :93 (the paused branch runs back at 1x whatever the speed).
void aPauseAt1_5xKeepsTheOffsetInMediaTime() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(0, 0, 400, 480);
    k.position(0, t);
    k.speed(1.5f);
    k.playing(true, t);
    k.position(1'500'000, t + 1'000 * kMs);
    k.offset(100);
    check(k.at(t + 1'000 * kMs) == 64'800, "1.5x: +100 ms of wall time is 150 ms of media behind");
    k.playing(false, t + 1'000 * kMs);
    check(k.at(t + 1'000 * kMs) == 64'800, "1.5x pause: the picture does not jump at the pause");
    check(k.at(t + 1'050 * kMs) == 68'400, "1.5x pause: 50 ms on it has caught up 75 ms of media");
    check(k.at(t + 1'100 * kMs) == 72'000, "1.5x pause: and holds at the pause once the offset passed");
}

// Kills :76 (the same speed again opens a new span) and :79 (a speed change, playing or paused, does
// not hold the offset at the change, as the header promises).
void aSpeedChangeHoldsThePictureForTheOffset() {
    const int64_t t = kUptimeNs;
    {
        Clock k;
        k.buffers(0, 0, 400, 480);
        k.position(0, t);
        k.playing(true, t);
        k.position(1'000'000, t + 1'000 * kMs);
        k.offset(100);
        check(k.at(t + 1'000 * kMs) == 43'200, "speed: +100 ms shows 0.9 s at 1x");
        k.speed(1.0f);  // Media3 hands the same parameters again
        check(k.at(t + 1'000 * kMs) == 43'200, "speed: the same speed again moves nothing");
        k.speed(2.0f);
        check(k.at(t + 1'000 * kMs) == 48'000, "speed: at a change the offset holds at the change");
        check(k.at(t + 1'050 * kMs) == 48'000, "speed: and keeps holding for the offset's length");
        check(k.at(t + 1'150 * kMs) == 52'800, "speed: then 50 ms at 2x is 100 ms of media on");
    }
    {
        Clock k;
        k.buffers(0, 0, 400, 480);
        k.position(0, t);
        k.playing(true, t);
        k.position(500'000, t + 500 * kMs);
        k.playing(false, t + 500 * kMs);
        k.offset(100);
        check(k.at(t + 500 * kMs) == 19'200, "speed while paused: +100 ms shows 0.4 s");
        k.speed(2.0f);
        check(k.at(t + 500 * kMs) == 24'000, "speed while paused: holds at the pause, not 200 ms behind it");
    }
}

// Kills :74 (zero, negative, NaN or infinite speed accepted).
void onlyAFinitePositiveSpeedApplies() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(0, 0, 400, 480);
    k.position(0, t);
    k.playing(true, t);
    k.speed(0.0f);
    k.speed(-1.0f);
    k.speed(std::numeric_limits<float>::quiet_NaN());
    k.speed(std::numeric_limits<float>::infinity());
    k.speed(-std::numeric_limits<float>::infinity());
    check(k.at(t + 100 * kMs) == 4'800, "speed: 0, -1, NaN and +-inf are ignored, 100 ms at 1x");
    k.speed(2.0f);
    check(k.at(t + 100 * kMs) == 9'600, "speed: 2x after them still applies");
}

// Kills :41 (the first report of a new map does not open the play span) and :70 (a flush keeps the
// old position): the sink's first report after a seek, while playing, comes 300 ms into the new item.
void aPositiveOffsetHoldsAtTheFirstReportAfterASeek() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(0, 0, 100, 480);
    k.position(0, t);
    k.playing(true, t);
    k.position(500'000, t + 500 * kMs);
    geode_playout_flush(k.c);
    k.buffers(30'000'000, 48'000, 100, 480);
    k.position(30'300'000, t + 700 * kMs);
    k.offset(100);
    check(k.at(t + 700 * kMs) == 62'400, "seek: +100 ms holds at the first report, 300 ms into the item");
    check(k.at(t + 800 * kMs) == 62'400, "seek: for the offset's length");
    check(k.at(t + 850 * kMs) == 64'800, "seek: then plays on from it");
}

// Kills :70 (a flush keeps `positioned`): a seek back from 30 s to the start of the item.
void aSeekBackHearsNothingOfTheNewItemBeforeItsFirstReport() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(30'000'000, 0, 100, 480);
    k.position(30'500'000, t);
    k.playing(true, t);
    geode_playout_flush(k.c);
    k.buffers(0, 48'000, 100, 480);
    check(k.at(t + 10 * kMs) == 48'000, "seek back: the new item's first sample until the sink reports");
}

// Kills :84 (the lower clamp dropped, or the limit off by one either way): the sink holds 4 s ahead.
void theOffsetIsClampedToASecondBothWays() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(0, 0, 400, 480);
    k.position(0, t);
    k.playing(true, t);
    k.offset(-5'000);
    check(k.at(t) == 48'000, "offset -5000 ms: a second early, no more");
    k.offset(INT_MIN);
    check(k.at(t) == 48'000, "offset INT_MIN: a second early, no more");
    k.position(2'000'000, t + 2'000 * kMs);
    k.offset(INT_MAX);
    check(k.at(t + 2'000 * kMs) == 48'000, "offset INT_MAX: a second late, no more");
}

// Kills :14 (a retry becomes a second anchor at the same time, or its >= made >) and :118 (< made <=,
// reading an anchor past the newest). Media starts at 10 s and the ring at 1000, so no slope is 0-based.
void aRetriedNewestBufferKeepsTheStreamEndRunning() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.handled(10'000'000, 1'000, 480);
    k.handled(10'010'000, 1'480, 480);
    k.handled(10'020'000, 1'960, 480);
    k.handled(10'020'000, 1'960, 480);  // the AudioTrack did not take it whole
    k.position(10'025'000, t);
    check(k.at(t) == 2'200, "stream end: 5 ms into the newest buffer, retried, is 240 samples into it");
}

// Kills :127 (two anchors treated as one, or one anchor given a slope it does not have).
void theNewestBufferRunsOnOnlyWithASlope() {
    const int64_t t = kUptimeNs;
    {
        Clock k;
        k.handled(5'000'000, 7'000, 480);
        k.position(5'005'000, t);
        check(k.at(t) == 7'000, "one anchor: no slope to trust, holds at its first sample");
    }
    {
        Clock k;
        k.handled(2'000'000, 0, 480);
        k.handled(2'010'000, 480, 480);
        k.position(2'015'000, t);
        check(k.at(t) == 720, "two anchors: 5 ms into the newest runs on at their slope");
    }
}

// Kills :14 (== made <=) and :20 (its media-time clause dropped): media time goes back while the ring
// goes on, so the map starts again from the new anchor.
void mediaTimeGoingBackStartsTheMapAgain() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.handled(1'000'000, 0, 480);
    k.handled(1'010'000, 480, 480);
    k.handled(500'000, 960, 480);
    k.position(400'000, t);
    check(k.at(t) == 960, "time went back: the old anchors are gone, the new one is the oldest");
}

// Kills :9 (negative frames kept): a buffer cannot un-write samples.
void aNegativeFrameCountWritesNothing() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.handled(0, 500, -480);
    k.position(1'000'000, t);
    check(k.at(t) == 500, "negative frames: the anchor's own sample, never before it");
}

// Kills :118-131 mutants that read one rate for the whole map (a fixed pair's slope, the tail from a
// pair other than the newest): a gapless change from 48 kHz to 44.1 kHz at 1 s.
void aGaplessRateChangeKeepsEachStretchsRate() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(0, 0, 100, 480);
    k.buffers(1'000'000, 48'000, 100, 441);
    const struct {
        int64_t us;
        int64_t want;
        const char* what;
    } cases[] = {
        {500'000, 24'000, "rate change: half a second at 48 kHz"},
        {999'999, 47'999, "rate change: a microsecond before it, the 48 kHz stretch's last sample"},
        {1'000'000, 48'000, "rate change: at it, the 44.1 kHz stretch's first sample"},
        {1'500'000, 70'050, "rate change: then half a second at 44.1 kHz"},
        {1'505'000, 70'270, "rate change: 5 ms into a 44.1 kHz buffer is 220 samples"},
        {1'995'000, 91'879, "rate change: inside the newest buffer it runs on at 44.1 kHz"},
        {3'000'000, 92'100, "rate change: past the end, the newest sample written and no further"},
    };
    for (const auto& c : cases) {
        k.position(c.us, t);
        check(k.at(t) == c.want, c.what);
    }
}

// Kills :99 mutants that drop nanoseconds (integer ns to us): one 48 kHz sample is 20 833.3 ns.
void nanosecondsAreNotRoundedToMicroseconds() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(0, 0, 200, 480);
    k.position(1'000'000, t);
    k.playing(true, t);
    check(k.at(t + 20'833) == 48'000, "20 833 ns is just short of one 48 kHz sample");
    check(k.at(t + 20'834) == 48'001, "20 834 ns is just past one");
    check(k.at(t + kMs) == 48'048, "a millisecond is 48 samples");
}

// Kills :26 (== made >: never evicting overwrites the oldest slot, so the window's oldest is one of its
// newest). 50 s of 10 ms buffers; the header promises at least 10 s of them kept.
void theAnchorWindowKeepsTheNewestAfterManyWraps() {
    const int64_t t = kUptimeNs;
    Clock k;
    k.buffers(0, 0, 5'000, 480);
    k.position(49'995'000, t);
    check(k.at(t) == 2'399'760, "window: 49.995 s in is sample 2 399 760");
    k.position(49'000'000, t);
    check(k.at(t) == 2'352'000, "window: 49 s in is sample 2 352 000");
    k.position(0, t);
    const int64_t oldest = k.at(t);
    check(oldest > 0, "window: the stream's start was dropped, the oldest kept is later");
    check(oldest <= 1'920'000, "window: and the oldest kept is at least 10 s before the newest");
}

// Kills any 32-bit narrowing of media time, ring index or monotonic time (the PR's other cases stay
// under 2^31 in ring and media time): an hour of play at 30 days' uptime, 10 h into a long mix, with
// the ring already past 2^31 samples, then the offset, a pause and a resume.
void anHourOfPlayAtThirtyDaysUptime() {
    constexpr int64_t kRingBase = 3'000'000'000;
    Sink s;
    s.nowNs = kUptimeNs;
    s.nextTickNs = kUptimeNs + kTickNs;
    s.ringBase = kRingBase;
    s.start(36'000'000'000);
    s.advance(3'600'000);
    checkNear(s.frame() - kRingBase, 172'800'000, 1, "hours: an hour at 48 kHz is 172 800 000 samples");
    tracks(s, 500, "hours: the speaker's sample an hour in");
    s.offset(1'000);
    checkNear(s.frame() - kRingBase, 172'776'000, 1, "hours: a second of offset is 48 000 samples back");
    s.offset(0);
    s.pause();
    s.advance(60'000);
    checkNear(s.frame() - kRingBase, 172'824'000, 1, "hours: a minute's pause holds");
    s.resume();
    s.advance(10);
    checkNear(s.frame() - kRingBase, 172'824'480, 1, "hours: resumed, 10 ms on");
}

// The sink's own clock runs 2 % slow or fast against monotonic time (its crystal, not ours). Between
// reports the clock moves on at 1x, so it may be off by at most 10 ms x 2 % = 9.6 samples (plus one for
// flooring); each report puts it back on the speaker, and it never runs backwards. Kills :45 (a report
// keeps the old report time, so the clock runs on from a stale origin).
void aDriftingSinkIsCorrectedAtEveryReport() {
    const double drifts[2] = {0.98, 1.02};
    for (const double drift : drifts) {
        const int64_t t = kUptimeNs;
        Clock k;
        k.buffers(0, 0, 1'000, 480);
        k.position(0, t);
        k.playing(true, t);
        int64_t last = 0;
        for (int64_t ms = 1; ms <= 5'000; ++ms) {
            const double playedUs = static_cast<double>(ms) * 1'000.0 * drift;
            if (ms % 10 == 0) k.position(std::llround(playedUs), t + ms * kMs);
            const int64_t v = k.at(t + ms * kMs);
            const auto heard = static_cast<int64_t>(std::floor(playedUs * 48.0 / 1'000.0));
            checkNear(v, heard, 11, "drift: within 10 ms of 2 % drift between reports");
            if (ms % 10 == 0) checkNear(v, heard, 1, "drift: a report puts it back on the speaker");
            check(v >= last, "drift: the correction never runs the speaker backwards");
            last = v;
        }
    }
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
    aStalledSinkHoldsAQuarterSecondPastItsLastReport();
    aPauseBetweenReportsKeepsWhatPlayedSinceTheLastOne();
    aRepeatedPlayOrPauseChangesNothing();
    aPauseAt1_5xKeepsTheOffsetInMediaTime();
    aSpeedChangeHoldsThePictureForTheOffset();
    onlyAFinitePositiveSpeedApplies();
    aPositiveOffsetHoldsAtTheFirstReportAfterASeek();
    aSeekBackHearsNothingOfTheNewItemBeforeItsFirstReport();
    theOffsetIsClampedToASecondBothWays();
    aRetriedNewestBufferKeepsTheStreamEndRunning();
    theNewestBufferRunsOnOnlyWithASlope();
    mediaTimeGoingBackStartsTheMapAgain();
    aNegativeFrameCountWritesNothing();
    aGaplessRateChangeKeepsEachStretchsRate();
    nanosecondsAreNotRoundedToMicroseconds();
    theAnchorWindowKeepsTheNewestAfterManyWraps();
    anHourOfPlayAtThirtyDaysUptime();
    aDriftingSinkIsCorrectedAtEveryReport();
    if (failures) {
        std::printf("%d playout check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("playout: all checks passed\n");
    return EXIT_SUCCESS;
}
