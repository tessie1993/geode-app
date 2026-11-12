// Host-side checks for the live wallpaper's and the dream's run rule (core/viz/SurfaceRun) through the C ABI
// the shell calls. Built and run by CI ("Native host tests" in .github/workflows/android.yml); no GL, no device.
//
// What is pinned: the rule's priority order (stopped beats still beats moving), the preview's exemptions,
// listening ignoring the cover and the silence, and the trial's end (#376): a trial that runs out turns a
// moving surface still, and the caller is told to restate just after that moment, never before. Then
// reduced motion (#441, ADR 0008): either switch holds a still frame on the wallpaper and on the dream, the
// preview too, and never wakes a stopped surface; the still frame's settle; and the per-surface fps clamp.
// Also the whole decision table (all 256 flag combinations under every entitlement), the trial's end at the
// exact millisecond on a real wall clock, the restate over seeded trials, and the 6 s silence clock
// (core/sound/Silence, through geode_silence_*) feeding the rule.
#include <cstdio>
#include <random>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kStopped = 0;
constexpr int kStill = 1;
constexpr int kAnimated = 2;
constexpr int kDark = 3;

constexpr unsigned kOnScreen = GEODE_SURFACE_HAS_SURFACE | GEODE_SURFACE_VISIBLE;
constexpr unsigned kDefaults = GEODE_SURFACE_PAUSE_IN_BATTERY_SAVER | GEODE_SURFACE_REST_WHEN_SILENT;
constexpr long long kPro = GEODE_SURFACE_FOREVER;
constexpr long long kFree = 0;
constexpr long long kNow = 1'000'000;

int mode(unsigned flags, long long until = kPro) { return geode_surface_run_mode(flags, until, kNow); }

void priorityOrder() {
    check(mode(kDefaults) == kStopped, "no surface, not visible: stopped");
    check(mode(GEODE_SURFACE_HAS_SURFACE | kDefaults) == kStopped, "not visible: stopped");
    check(mode(GEODE_SURFACE_VISIBLE | kDefaults) == kStopped, "no surface: stopped");
    check(mode(kOnScreen | kDefaults) == kAnimated, "on screen, Pro: moving");
    check(mode(kOnScreen | kDefaults | GEODE_SURFACE_COVERED) == kStopped, "covered: stopped");
    check(mode(kOnScreen | kDefaults, kFree) == kStill, "free: still");
    check(mode(kOnScreen | kDefaults | GEODE_SURFACE_COVERED, kFree) == kStopped, "covered beats free");
    check(mode(kOnScreen | kDefaults | GEODE_SURFACE_BATTERY_SAVER) == kStill, "battery saver: still");
    check(mode(kOnScreen | GEODE_SURFACE_BATTERY_SAVER) == kAnimated, "battery saver, option off: moving");
    check(mode(kOnScreen | kDefaults | GEODE_SURFACE_SILENT) == kStill, "silent: still");
    check(mode(kOnScreen | GEODE_SURFACE_SILENT) == kAnimated, "silent, rest option off: moving");
}

void preview() {
    const unsigned p = kOnScreen | kDefaults | GEODE_SURFACE_PREVIEW;
    check(mode(p | GEODE_SURFACE_COVERED) == kAnimated, "preview ignores the cover");
    check(mode(p | GEODE_SURFACE_BATTERY_SAVER | GEODE_SURFACE_SILENT) == kAnimated, "preview never rests");
    check(mode(p, kFree) == kStill, "preview without Pro: still");
}

void listening() {
    const unsigned f = kOnScreen | kDefaults;
    check(geode_surface_listening(f | GEODE_SURFACE_COVERED, kPro, kNow) == 1, "covered keeps listening");
    check(geode_surface_listening(f | GEODE_SURFACE_SILENT, kPro, kNow) == 1, "silence keeps listening");
    check(geode_surface_listening(f | GEODE_SURFACE_BATTERY_SAVER, kPro, kNow) == 0, "battery saver hears nothing");
    check(geode_surface_listening(f, kFree, kNow) == 0, "free hears nothing");
    check(geode_surface_listening(GEODE_SURFACE_HAS_SURFACE | kDefaults, kPro, kNow) == 0, "hidden hears nothing");
}

void trialEnd() {
    const unsigned f = kOnScreen | kDefaults;
    const long long ends = kNow + 60'000;
    check(geode_surface_run_mode(f, ends, kNow) == kAnimated, "running trial: moving");
    check(geode_surface_run_mode(f, ends, ends - 1) == kAnimated, "trial's last millisecond: moving");
    check(geode_surface_run_mode(f, ends, ends) == kStill, "trial over: still");
    check(geode_surface_entitled(ends, kNow) == 1 && geode_surface_entitled(ends, ends) == 0, "dream ends with the trial");

    const long long wait = geode_surface_recheck_in_ms(ends, kNow);
    check(wait > ends - kNow, "restate lands after the end, never before");
    check(wait <= ends - kNow + 2'000, "restate lands soon after the end");
    check(geode_surface_run_mode(f, ends, kNow + wait) == kStill, "the restate finds it still");

    check(geode_surface_recheck_in_ms(kPro, kNow) == -1, "Pro never rechecks");
    check(geode_surface_recheck_in_ms(kFree, kNow) == -1, "free never rechecks");
    check(geode_surface_recheck_in_ms(ends, ends + 5) == -1, "a trial already over never rechecks");
}

// ---- The full decision table and its transitions over time ---------------------------------------------
//
// Expected values come from the rule as AGENTS.md states it for `wallpaper` ("stopped when hidden or covered
// by the shade or recents, a still frame without Pro or once a trial ends, in battery saver or after 6 s of
// silence"), the preview's exemptions in SurfaceRun.hpp's header comment, Kotlin's TrialState.at (a trial is
// active for `now in startedAt until endsAt`, a half-open range: the end itself is over), and Silence.hpp's
// "nothing heard for longer than kRestAfterMs" with AGENTS.md's 6 s.

constexpr unsigned kEveryBit = 0xFFu;  // the eight GEODE_SURFACE_* bits

// A real wall clock (Nov 2026, ms since the epoch): far past what 32 bits hold.
constexpr long long kWall = 1'793'000'000'000LL;
constexpr long long kHour = 3'600'000;  // the trial's length

bool on(unsigned flags, unsigned bit) { return (flags & bit) != 0; }

void checkAt(bool ok, const char* what, unsigned flags, long long until) {
    if (ok) return;
    char line[200];
    std::snprintf(line, sizeof line, "%s (flags 0x%02x, until %lld)", what, flags, until);
    check(false, line);
}

// The entitlements a surface can hold at kWall: Pro, free, a trial with time left, a trial over this very
// millisecond, a trial over a millisecond ago.
struct Entitlement {
    long long until;
    bool moves;
};
constexpr Entitlement kEntitlements[] = {
    {GEODE_SURFACE_FOREVER, true}, {0, false}, {kWall + kHour, true}, {kWall, false}, {kWall - 1, false}};

// Every one of the 256 flag combinations under every entitlement, row by row of the stated rule.
// Kills: || -> && on surface/visible, a dropped surface or visible check, the cover not stopping (or
// stopping the preview), the entitlement read after the cover or after the preview, free -> Stopped, the
// preview not exempt, battery saver or silence ignoring its option (or stopping instead of stilling), and a
// 32-bit truncation of animate_until_ms (kWall + kHour does not fit an int).
void decisionTableEveryCombination() {
    for (const Entitlement& e : kEntitlements) {
        for (unsigned f = 0; f <= kEveryBit; ++f) {
            const int m = geode_surface_run_mode(f, e.until, kWall);
            const bool shown = on(f, GEODE_SURFACE_HAS_SURFACE) && on(f, GEODE_SURFACE_VISIBLE);
            const bool preview = on(f, GEODE_SURFACE_PREVIEW);
            if (!shown) {
                checkAt(m == kStopped, "hidden or no surface: stopped", f, e.until);
            } else if (on(f, GEODE_SURFACE_COVERED) && !preview) {
                checkAt(m == kStopped, "covered by the shade or recents: stopped", f, e.until);
            } else if (!e.moves) {
                checkAt(m == kStill, "on screen without Pro or once a trial ends: a still frame", f, e.until);
            } else if (preview) {
                checkAt(m == kAnimated, "the picker's preview, entitled: always moving", f, e.until);
            } else {
                const bool saver = on(f, GEODE_SURFACE_BATTERY_SAVER) && on(f, GEODE_SURFACE_PAUSE_IN_BATTERY_SAVER);
                const bool rest = on(f, GEODE_SURFACE_SILENT) && on(f, GEODE_SURFACE_REST_WHEN_SILENT);
                checkAt(m == ((saver || rest) ? kStill : kAnimated),
                        "entitled, uncovered: still exactly in battery saver or a silent rest (each with its option)",
                        f, e.until);
            }
        }
    }
}

// More reasons to rest never make a surface livelier (stopped < still < moving): hiding it, covering it,
// battery saver, silence, turning either rest option on, or losing the entitlement. The preview is exempt
// from the cover, the saver and the silence by design, so those are walked without it.
// Kills: any negated condition (e.g. !battery_saver && pause), a rest branch returning Animated.
void restingNeverMakesItLivelier() {
    const unsigned restBits[] = {GEODE_SURFACE_COVERED, GEODE_SURFACE_BATTERY_SAVER, GEODE_SURFACE_SILENT,
                                 GEODE_SURFACE_PAUSE_IN_BATTERY_SAVER, GEODE_SURFACE_REST_WHEN_SILENT};
    const unsigned sightBits[] = {GEODE_SURFACE_HAS_SURFACE, GEODE_SURFACE_VISIBLE};
    for (unsigned f = 0; f <= kEveryBit; ++f) {
        for (const Entitlement& e : kEntitlements) {
            const int m = geode_surface_run_mode(f, e.until, kWall);
            if (!on(f, GEODE_SURFACE_PREVIEW)) {
                for (unsigned b : restBits) {
                    checkAt(geode_surface_run_mode(f | b, e.until, kWall) <= m, "a rest reason never livens it", f,
                            e.until);
                }
            }
            for (unsigned b : sightBits) {
                checkAt(geode_surface_run_mode(f & ~b, e.until, kWall) == kStopped, "out of sight: stopped", f,
                        e.until);
            }
            checkAt(geode_surface_run_mode(f, 0, kWall) <= m, "losing the entitlement never livens it", f, e.until);
        }
    }
}

// Listening, over every combination: the shade and the silence never change whether it hears (a glance at
// the shade keeps the capture, and hearing is what ends a silent rest); anything moving hears; nothing
// unentitled or out of sight hears. Kills: a dropped cover or silence mask, == Animated -> != Stopped, and a
// mask that also strips the preview (a preview in battery saver moves, so it must hear).
void listeningOverEveryCombination() {
    for (const Entitlement& e : kEntitlements) {
        for (unsigned f = 0; f <= kEveryBit; ++f) {
            const int hears = geode_surface_listening(f, e.until, kWall);
            checkAt(hears == geode_surface_listening(f ^ GEODE_SURFACE_COVERED, e.until, kWall),
                    "the shade never changes listening", f, e.until);
            checkAt(hears == geode_surface_listening(f ^ GEODE_SURFACE_SILENT, e.until, kWall),
                    "silence never changes listening", f, e.until);
            if (geode_surface_run_mode(f, e.until, kWall) == kAnimated) {
                checkAt(hears == 1, "a moving surface hears sound", f, e.until);
            }
            if (!e.moves) checkAt(hears == 0, "without the entitlement nothing is heard", f, e.until);
            if (!on(f, GEODE_SURFACE_HAS_SURFACE) || !on(f, GEODE_SURFACE_VISIBLE)) {
                checkAt(hears == 0, "out of sight nothing is heard", f, e.until);
            }
        }
    }
    const unsigned preview = kOnScreen | kDefaults | GEODE_SURFACE_PREVIEW | GEODE_SURFACE_BATTERY_SAVER;
    check(geode_surface_listening(preview, kPro, kWall) == 1, "the preview in battery saver keeps hearing");
}

// The trial's end at the exact millisecond, for moving, listening, the preview and the dream alike, on a
// real wall clock. Kills: < -> <= in entitled, listening judged at a fixed time instead of now, the
// preview's moving branch placed before the entitlement, int truncation of the ABI's long long.
void trialEndsAtTheExactMillisecond() {
    const unsigned f = kOnScreen | kDefaults;
    const unsigned p = f | GEODE_SURFACE_PREVIEW;
    const long long started = kWall;
    const long long ends = started + kHour;
    check(geode_surface_run_mode(f, ends, started) == kAnimated, "a trial's first ms: moving");
    check(geode_surface_run_mode(f, ends, ends - 1) == kAnimated, "a 1 h trial at 59:59.999: moving");
    check(geode_surface_run_mode(f, ends, ends) == kStill, "a 1 h trial at 60:00.000: still");
    check(geode_surface_run_mode(p, ends, ends - 1) == kAnimated, "the preview in a trial's last ms: moving");
    check(geode_surface_run_mode(p, ends, ends) == kStill, "the preview once the trial ends: still");
    check(geode_surface_listening(f, ends, ends - 1) == 1, "a trial's last ms: listening");
    check(geode_surface_listening(f, ends, ends) == 0, "once the trial ends the capture is released");
    check(geode_surface_entitled(ends, ends - 1) == 1, "the dream runs through the trial's last ms");
    check(geode_surface_entitled(ends, ends) == 0, "the dream ends on the trial's end");
    check(geode_surface_entitled(GEODE_SURFACE_FOREVER, kWall) == 1, "Pro dreams on a real clock");
    check(geode_surface_entitled(0, kWall) == 0, "free never dreams");
    // Hidden over the end, shown after it (the phone woke past the end): the first restate is still.
    check(geode_surface_run_mode(GEODE_SURFACE_HAS_SURFACE | kDefaults, ends, ends - 1) == kStopped,
          "hidden near the end: stopped");
    check(geode_surface_run_mode(f, ends, ends + 30'000) == kStill, "shown after the end: still");
}

// The restate the services post, over seeded trials on real wall clocks: always after the end, soon after
// it, by the same margin however much is left, and once it fires nothing more is asked. A late fire
// (Handler.postDelayed counts uptime, which stops in deep sleep) still reads still; an early one (the wall
// clock set back) asks again rather than never. Kills: slack 0 or negative, slack scaled with the time
// left, the wait measured from anything but now, recheck's own < -> <= at the end, a dropped "already over"
// check, int truncation.
void restateLandsJustAfterEveryTrialEnd() {
    const unsigned f = kOnScreen | kDefaults;
    std::mt19937_64 gen(376);
    long long firstMargin = -1;
    for (int i = 0; i < 2000; ++i) {
        const long long now = kWall + static_cast<long long>(gen() % 100'000'000'000ULL);
        const long long left = 1 + static_cast<long long>(gen() % static_cast<unsigned long long>(kHour));
        const long long ends = now + left;
        const long long wait = geode_surface_recheck_in_ms(ends, now);
        const long long margin = wait - left;
        checkAt(margin > 0, "the restate lands after the end, never on or before it", f, ends);
        checkAt(margin <= 2'000, "the restate lands within 2 s of the end", f, ends);
        if (firstMargin < 0) firstMargin = margin;
        checkAt(margin == firstMargin, "the same margin past the end however much trial is left", f, ends);
        checkAt(geode_surface_run_mode(f, ends, now + wait) == kStill, "the restate finds it still", f, ends);
        checkAt(geode_surface_recheck_in_ms(ends, now + wait) == -1, "after the restate nothing more is asked", f,
                ends);
        const long long late = now + wait + static_cast<long long>(gen() % 86'400'000ULL);
        checkAt(geode_surface_run_mode(f, ends, late) == kStill && geode_surface_recheck_in_ms(ends, late) == -1,
                "a restate that fires late (deep sleep) still finds it still", f, ends);
        const long long early = now + static_cast<long long>(gen() % static_cast<unsigned long long>(left));
        checkAt(geode_surface_recheck_in_ms(ends, early) > ends - early, "an early restate asks again, after the end",
                f, ends);
    }
    const long long ends = kWall + kHour;
    check(geode_surface_recheck_in_ms(ends, ends - 1) > 1, "in the trial's last ms a restate is still asked for");
    check(geode_surface_recheck_in_ms(ends, ends) == -1, "on the end's own ms nothing more will change");
    check(geode_surface_recheck_in_ms(GEODE_SURFACE_FOREVER, kWall) == -1, "Pro on a real clock never rechecks");
}

// ---- The silence clock (core/sound/Silence, geode_silence_*) feeding the run rule ----------------------

// One poll the way SoundSource makes it: the newest stereo block, then whether the quiet has lasted.
struct Listener {
    long long heardAt = 0;
    bool poll(float left, float right, long long now) {
        float l[64];
        float r[64];
        for (int i = 0; i < 64; ++i) {
            l[i] = (i == 17) ? left : 0.0f;  // one sample carries the level, the rest are digital silence
            r[i] = (i == 41) ? right : 0.0f;
        }
        const float* ch[2] = {l, r};
        heardAt = geode_silence_heard_at(ch, 2, 64, now, heardAt);
        return geode_silence_resting(now, heardAt) == 1;
    }
};

// Music, then quiet: the wallpaper keeps moving through 6 s of silence, stills on the first ms past it,
// keeps listening while it rests, and the next audible poll wakes it. Kills: > -> >= in resting, the 6 s
// off by a ms either way, heardAt always now or never now, the grace not restarting from the last sound.
void silenceRestsAfterSixSecondsAndSoundWakesIt() {
    const unsigned f = kOnScreen | kDefaults;
    const long long t0 = kWall;
    Listener ear;
    ear.heardAt = t0 - 60'000;
    for (long long t = t0 - 5'000; t <= t0; t += 250) check(!ear.poll(0.3f, 0.3f, t), "music: never resting");
    auto modeAt = [&](long long t) {
        const bool silent = ear.poll(0.0f, 0.0f, t);
        return geode_surface_run_mode(f | (silent ? GEODE_SURFACE_SILENT : 0u), kPro, t);
    };
    check(modeAt(t0 + 1'000) == kAnimated, "1 s of quiet: moving");
    check(modeAt(t0 + 5'999) == kAnimated, "5.999 s of quiet: moving");
    check(modeAt(t0 + 6'000) == kAnimated, "exactly 6 s of quiet: not yet longer than the grace");
    check(modeAt(t0 + 6'001) == kStill, "6.001 s of quiet: a still frame");
    check(modeAt(t0 + 60'000) == kStill, "a minute of quiet: still a still frame");
    check(geode_surface_listening(f | GEODE_SURFACE_SILENT, kPro, t0 + 60'000) == 1, "a resting surface listens");
    const bool silent = ear.poll(0.2f, 0.0f, t0 + 60'250);
    check(!silent, "the first audible poll ends the rest");
    check(geode_surface_run_mode(f, kPro, t0 + 60'250) == kAnimated, "sound back: moving again");
    check(!ear.poll(0.0f, 0.0f, t0 + 66'250) && ear.poll(0.0f, 0.0f, t0 + 66'251),
          "the grace restarts from the last sound heard");
    // The options: rest-when-silent off never rests; the preview never rests.
    check(geode_surface_run_mode(kOnScreen | GEODE_SURFACE_PAUSE_IN_BATTERY_SAVER | GEODE_SURFACE_SILENT, kPro,
                                 t0 + 66'251) == kAnimated,
          "rest when silent off: moving through the quiet");
    check(geode_surface_run_mode(f | GEODE_SURFACE_SILENT | GEODE_SURFACE_PREVIEW, kPro, t0 + 66'251) == kAnimated,
          "the preview in a quiet room: moving");
}

// What counts as sound: the level, either sign, either channel. Silence.hpp puts the line near -42 dBFS.
// Kills: a dropped fabs, > kPeak -> < kPeak, only the first channel read, a null channel ending the scan,
// an empty poll counted as sound.
void whatCountsAsSound() {
    Listener ear;
    const long long t = kWall;
    ear.heardAt = t - 10'000;
    ear.poll(0.005f, 0.0f, t);  // about -46 dBFS: under the line
    check(ear.heardAt == t - 10'000, "-46 dBFS is silence");
    ear.poll(0.01f, 0.0f, t);  // -40 dBFS: quiet music
    check(ear.heardAt == t, "-40 dBFS is sound");
    ear.heardAt = t - 10'000;
    ear.poll(-0.5f, 0.0f, t);
    check(ear.heardAt == t, "a negative peak is sound");
    ear.heardAt = t - 10'000;
    ear.poll(0.0f, 0.5f, t);
    check(ear.heardAt == t, "sound on the right channel alone is sound");
    const float* none[2] = {nullptr, nullptr};
    check(geode_silence_heard_at(none, 2, 64, t, t - 10'000) == t - 10'000, "no channels: nothing heard");
    float loud[4] = {0.9f, 0.9f, 0.9f, 0.9f};
    const float* one[2] = {nullptr, loud};
    check(geode_silence_heard_at(one, 2, 4, t, t - 10'000) == t, "a missing channel does not hide the other");
    check(geode_silence_heard_at(one, 2, 0, t, t - 10'000) == t - 10'000, "an empty poll hears nothing");
}

// A capture's digital silence (geode_capture_*): a refused capture writes exact zeros, so anything above
// digital zero is sound, and 4 s of only zeros since the start or the last sound is quiet. Kills: > -> >=
// in quiet, the 4 s off by a ms, a dropped fabs, a floor raised to the wallpaper's -42 dBFS.
void captureQuietAfterFourSecondsOfZeros() {
    const long long t0 = kWall;
    long long heardAt = t0;  // the capture's start
    for (long long t = t0; t <= t0 + 4'000; t += 21) {
        heardAt = geode_capture_heard_at(0.0f, t, heardAt);
        check(geode_capture_quiet(t, heardAt) == 0, "zeros for up to 4 s: not yet quiet");
    }
    heardAt = geode_capture_heard_at(0.0f, t0 + 4'001, heardAt);
    check(geode_capture_quiet(t0 + 4'001, heardAt) == 1, "zeros past 4 s: quiet");
    heardAt = geode_capture_heard_at(0.004f, t0 + 4'020, heardAt);
    check(heardAt == t0 + 4'020 && geode_capture_quiet(t0 + 4'020, heardAt) == 0,
          "quiet music (-48 dBFS) is sound to a capture");
    check(geode_capture_heard_at(-0.5f, t0, t0 - 9'000) == t0, "a negative peak is sound");
    check(geode_capture_heard_at(1e-7f, t0, t0 - 9'000) == t0 - 9'000, "dither under digital zero is not");
    check(geode_capture_quiet(t0 + 8'020, t0 + 4'020) == 0 && geode_capture_quiet(t0 + 8'021, t0 + 4'020) == 1,
          "the 4 s restart from the last sound");
}

// The trial runs out during a silent rest: the still frame stays, and the capture is released, so music
// coming back after the end does not move it. Kills: listening ignoring the entitlement during a rest.
void trialEndsDuringASilentRest() {
    const unsigned f = kOnScreen | kDefaults | GEODE_SURFACE_SILENT;
    const long long ends = kWall + kHour;
    check(geode_surface_run_mode(f, ends, ends - 1) == kStill, "resting in the trial's last ms");
    check(geode_surface_listening(f, ends, ends - 1) == 1, "listening in the trial's last ms");
    check(geode_surface_run_mode(f, ends, ends) == kStill, "still once the trial ends");
    check(geode_surface_listening(f, ends, ends) == 0, "the trial's end stops the listening");
    check(geode_surface_run_mode(kOnScreen | kDefaults, ends, ends) == kStill, "music back after the end: still");
}

// The dream restates with the surface fixed and never as a preview (it has a window, not a picker); the
// wallpaper with whatever its engine sees. Both must come out still under reduced motion.
constexpr unsigned kDream = kOnScreen | kDefaults;
constexpr unsigned kWallpaper = kOnScreen | kDefaults;
constexpr unsigned kReduced = GEODE_SURFACE_REDUCED_MOTION;

void reducedMotion() {
    check(mode(kWallpaper | kReduced) == kStill, "wallpaper, reduced motion: still");
    check(mode(kDream | kReduced) == kStill, "dream, reduced motion: still");
    check(mode(kOnScreen | kReduced) == kStill, "reduced motion with every rest option off: still");
    check(mode(kWallpaper | kReduced | GEODE_SURFACE_PREVIEW) == kStill, "preview under reduced motion: still");
    check(mode(kWallpaper | kReduced | GEODE_SURFACE_PREVIEW | GEODE_SURFACE_COVERED) == kStill,
          "a covered preview stays the picker's: still, not stopped");

    // Stopped beats still: reduced motion never draws on a surface nobody can see.
    check(mode(GEODE_SURFACE_HAS_SURFACE | kDefaults | kReduced) == kStopped, "reduced motion, not visible: stopped");
    check(mode(GEODE_SURFACE_VISIBLE | kDefaults | kReduced) == kStopped, "reduced motion, no surface: stopped");
    check(mode(kWallpaper | kReduced | GEODE_SURFACE_COVERED) == kStopped, "reduced motion, covered: stopped");
    check(mode(kDream | kReduced | GEODE_SURFACE_COVERED, kFree) == kStopped, "covered beats free and reduced motion");

    // Still with or without the other reasons; none of them turns it back to moving.
    check(mode(kWallpaper | kReduced, kFree) == kStill, "reduced motion and free: still");
    check(mode(kWallpaper | kReduced | GEODE_SURFACE_SILENT | GEODE_SURFACE_BATTERY_SAVER) == kStill,
          "reduced motion, silent, battery saver: still");

    // A surface that cannot move hears nothing, so the capture is released: the still frame no longer
    // reacts to the music (the owner's call for vestibular safety).
    check(geode_surface_listening(kWallpaper | kReduced, kPro, kNow) == 0, "reduced motion hears nothing");
    check(geode_surface_listening(kDream | kReduced | GEODE_SURFACE_SILENT, kPro, kNow) == 0,
          "reduced motion in silence hears nothing");

    // The trial's restate is about the entitlement alone, reduced motion or not.
    const long long ends = kNow + 60'000;
    check(geode_surface_run_mode(kDream | kReduced, ends, kNow) == kStill, "running trial, reduced motion: still");
    check(geode_surface_entitled(ends, kNow) == 1, "reduced motion does not end the dream");
}

// ---- The dream goes dark in silence (#999 F11) -----------------------------------------------------
//
// Owner: "No sound no visuals". The dream asks for GEODE_SURFACE_DARK_WHEN_SILENT, and where the wallpaper
// rests on a still frame it shows nothing; anything else is the wallpaper's rule unchanged. Kills: the dark
// branch without its silence or rest option, placed before the sight test or the cover (a covered dream
// drawn), before the entitlement (a free dream dark, not still), after reduced motion or battery saver (a
// still frame in silence), a dark preview, a bit that overlaps another, and listening losing the silence.

constexpr unsigned kDarkDream = kDream | GEODE_SURFACE_DARK_WHEN_SILENT;

void dreamGoesDarkInSilence() {
    check(mode(kDarkDream) == kAnimated, "dream with sound: moving");
    check(mode(kDarkDream | GEODE_SURFACE_SILENT) == kDark, "dream in silence: dark");
    check(mode(kWallpaper | GEODE_SURFACE_SILENT) == kStill, "wallpaper in silence: still frame, as before");
    check(mode(kOnScreen | GEODE_SURFACE_DARK_WHEN_SILENT | GEODE_SURFACE_SILENT) == kAnimated,
          "rest when silent off: moving through the quiet");
    check(mode(kDarkDream | GEODE_SURFACE_SILENT | kReduced) == kDark, "silence beats reduced motion: dark");
    check(mode(kDarkDream | GEODE_SURFACE_SILENT | GEODE_SURFACE_BATTERY_SAVER) == kDark,
          "silence beats battery saver: dark");
    check(mode(kDarkDream | GEODE_SURFACE_BATTERY_SAVER) == kStill, "battery saver with sound: still");
    check(mode(kDarkDream | kReduced) == kStill, "reduced motion with sound: still");
    check(mode(kDarkDream | GEODE_SURFACE_SILENT | GEODE_SURFACE_COVERED) == kStopped, "covered beats dark");
    check(mode(GEODE_SURFACE_HAS_SURFACE | kDefaults | GEODE_SURFACE_DARK_WHEN_SILENT | GEODE_SURFACE_SILENT) ==
              kStopped,
          "not dreaming: stopped");
    check(mode(kDarkDream | GEODE_SURFACE_SILENT, kFree) == kStill, "without the entitlement: still (the dream ends)");
    check(geode_surface_listening(kDarkDream | GEODE_SURFACE_SILENT, kPro, kNow) == 1, "a dark dream listens");
    check(geode_surface_listening(kDarkDream | GEODE_SURFACE_SILENT | kReduced, kPro, kNow) == 0,
          "dark under reduced motion hears nothing, as its still frame did");

    // Every combination of the other nine bits under every entitlement: the bit changes exactly the silent
    // rest, from a still frame to dark, and never listening.
    constexpr unsigned kNineBits = 0x1FFu;
    for (const Entitlement& e : kEntitlements) {
        for (unsigned f = 0; f <= kNineBits; ++f) {
            const int wallpaper = geode_surface_run_mode(f, e.until, kWall);
            const int dream = geode_surface_run_mode(f | GEODE_SURFACE_DARK_WHEN_SILENT, e.until, kWall);
            const bool shown = on(f, GEODE_SURFACE_HAS_SURFACE) && on(f, GEODE_SURFACE_VISIBLE);
            const bool preview = on(f, GEODE_SURFACE_PREVIEW);
            const bool rests = on(f, GEODE_SURFACE_SILENT) && on(f, GEODE_SURFACE_REST_WHEN_SILENT);
            const bool dark = shown && !preview && !on(f, GEODE_SURFACE_COVERED) && e.moves && rests;
            checkAt(dream == (dark ? kDark : wallpaper), "dark exactly in an entitled, uncovered silent rest", f,
                    e.until);
            if (dark) checkAt(wallpaper == kStill, "where the dream is dark the wallpaper is still", f, e.until);
            checkAt(geode_surface_listening(f | GEODE_SURFACE_DARK_WHEN_SILENT, e.until, kWall) ==
                        geode_surface_listening(f, e.until, kWall),
                    "dark never changes listening", f, e.until);
        }
    }

    // In and out of dark: nothing to settle, since there is no picture; out of it into a still frame (sound
    // back in battery saver) the scene settles like any way into still.
    check(geode_surface_settle_ms(kAnimated, kDark) == 0, "moving to dark: no settle");
    check(geode_surface_settle_ms(kStill, kDark) == 0, "still to dark: no settle");
    check(geode_surface_settle_ms(kDark, kAnimated) == 0, "dark to moving: no settle");
    check(geode_surface_settle_ms(kDark, kStill) == 2'000, "dark to still: settle 2 s");

    // The silence clock feeding it: dark on the first ms past the 6 s grace, sound wakes it.
    Listener ear;
    const long long t0 = kWall;
    ear.heardAt = t0;
    auto modeAt = [&](long long t, float level) {
        const bool silent = ear.poll(level, level, t);
        return geode_surface_run_mode(kDarkDream | (silent ? GEODE_SURFACE_SILENT : 0u), kPro, t);
    };
    check(modeAt(t0 + 6'000, 0.0f) == kAnimated, "6 s of quiet: moving");
    check(modeAt(t0 + 6'001, 0.0f) == kDark, "6.001 s of quiet: dark");
    check(modeAt(t0 + 60'000, 0.3f) == kAnimated, "sound back: moving again");
}

void settle() {
    check(geode_surface_settle_ms(kAnimated, kStill) == 2'000, "moving to still: settle 2 s");
    check(geode_surface_settle_ms(kStopped, kStill) == 2'000, "stopped to still: settle 2 s (a fresh scene builds)");
    check(geode_surface_settle_ms(kStill, kStill) == 0, "already still: one frame");
    check(geode_surface_settle_ms(kStill, kAnimated) == 0, "to moving: no settle");
    check(geode_surface_settle_ms(kStill, kStopped) == 0, "to stopped: no settle");
    check(geode_surface_settle_ms(kAnimated, kAnimated) == 0, "moving stays moving: no settle");
}

void fps() {
    check(GEODE_SURFACE_DEFAULT_FPS == 30, "default 30 (owner, ADR 0008)");
    check(geode_surface_fps_choice(0) == 30, "unset: the default");
    check(geode_surface_fps_choice(-5) == 30, "negative: the default");
    check(geode_surface_fps_choice(15) == 15, "15 is listed");
    check(geode_surface_fps_choice(30) == 30, "30 is listed");
    check(geode_surface_fps_choice(60) == 60, "60 is listed");
    check(geode_surface_fps_choice(1) == 15, "below the list: the lowest");
    check(geode_surface_fps_choice(240) == 60, "above the list: the highest");
    check(geode_surface_fps_choice(24) == 30, "24 snaps to the nearest, 30");
    check(geode_surface_fps_choice(45) == 30, "a tie takes the lower rate");
    check(geode_surface_fps_choice(50) == 60, "50 snaps to 60");

    int out[8] = {};
    const int n = geode_surface_fps_choices(out, 8);
    check(n == 3 && out[0] == 15 && out[1] == 30 && out[2] == 60, "the list: 15, 30, 60");
    bool listed = true;
    for (int i = 0; i < n; ++i) listed = listed && geode_surface_fps_choice(out[i]) == out[i];
    check(listed, "every listed rate is its own choice");
    int one[1] = {};
    check(geode_surface_fps_choices(one, 1) == 3 && one[0] == 15, "a short buffer gets the head, the count is whole");
    check(geode_surface_fps_choices(nullptr, 0) == 3, "a null buffer only counts");
}

}  // namespace

// The app's own stage (core/viz/StageRun): it draws while somebody can see it, not only while resumed, and
// exactly one surface counts the auto-director's style moments.
void stage() {
    const unsigned started = GEODE_STAGE_STARTED;
    const unsigned resumed = started | GEODE_STAGE_RESUMED;
    const auto draws = [](unsigned f) { return (geode_stage_run(f) & GEODE_STAGE_DRAWS) != 0; };
    const auto castLeads = [](unsigned f) { return (geode_stage_run(f) & GEODE_STAGE_CAST_LEADS_AUTO) != 0; };
    check(draws(resumed), "resumed: the stage draws");
    check(!draws(started), "paused behind a dialog: the stage rests");
    check(draws(started | GEODE_STAGE_PIP), "paused in a PiP window: the stage keeps drawing");
    check(draws(started | GEODE_STAGE_SECOND_SCREEN), "paused with a second screen: the stage keeps drawing");
    check(!draws(0), "stopped: the stage rests");
    check(!draws(GEODE_STAGE_PIP | GEODE_STAGE_SECOND_SCREEN | GEODE_STAGE_RESUMED), "stopped beats every reason to draw");
    check(!draws(GEODE_STAGE_CASTING), "a cast never keeps the hidden stage drawing");
    check(castLeads(GEODE_STAGE_CASTING), "casting in the background: the cast counts the moments");
    check(castLeads(started | GEODE_STAGE_CASTING), "casting behind a dialog: the cast counts the moments");
    check(!castLeads(resumed | GEODE_STAGE_CASTING), "casting with the stage on screen: the stage counts them");
    check(!castLeads(started | GEODE_STAGE_PIP | GEODE_STAGE_CASTING), "casting from a PiP window: the stage counts them");
    check(!castLeads(0), "no cast: nothing leads in the background");
    // The whole table: stopped never draws; the cast leads exactly when it runs and the stage does not; and
    // an unknown bit changes nothing.
    for (unsigned f = 0; f < 32; ++f) {
        const bool expectDraws = (f & GEODE_STAGE_STARTED) != 0 &&
            (f & (GEODE_STAGE_RESUMED | GEODE_STAGE_PIP | GEODE_STAGE_SECOND_SCREEN)) != 0;
        check(draws(f) == expectDraws, "stage table: draws");
        check(castLeads(f) == ((f & GEODE_STAGE_CASTING) != 0 && !expectDraws), "stage table: cast leads");
        check(!(draws(f) && castLeads(f)), "never two surfaces counting the moments");
        check(geode_stage_run(f | (1u << 9)) == geode_stage_run(f), "an unknown bit is ignored");
    }
}

// The connected display (core/viz/StageRun): only a presentation display that is neither the built-in screen
// nor the app's own; the user's pick while it qualifies; one that keeps refusing is skipped.
void connectedDisplay() {
    const unsigned ok = GEODE_STAGE_DISPLAY_PRESENTATION | GEODE_STAGE_DISPLAY_ON;
    const auto failed = [](unsigned n) { return n << GEODE_STAGE_DISPLAY_FAILURE_SHIFT; };
    check(geode_stage_display_eligible(ok) == 1, "an HDMI display qualifies");
    check(geode_stage_display_eligible(GEODE_STAGE_DISPLAY_PRESENTATION) == 1, "a display that is off still qualifies");
    check(geode_stage_display_eligible(GEODE_STAGE_DISPLAY_ON) == 0, "no FLAG_PRESENTATION: a presentation would be refused");
    check(geode_stage_display_eligible(ok | GEODE_STAGE_DISPLAY_DEFAULT) == 0, "never the built-in screen");
    check(geode_stage_display_eligible(ok | GEODE_STAGE_DISPLAY_HOST) == 0, "never the display the app itself is on");
    check(geode_stage_display_eligible(ok | failed(2)) == 1, "two failures: try again");
    check(geode_stage_display_eligible(ok | failed(3)) == 0, "three failures running: skipped");

    const int ids[] = {0, 2, 5, 7};
    const unsigned flags[] = {
        ok | GEODE_STAGE_DISPLAY_DEFAULT,
        GEODE_STAGE_DISPLAY_PRESENTATION,
        ok,
        ok | GEODE_STAGE_DISPLAY_HOST,
    };
    check(geode_stage_pick_display(ids, flags, 4, -1) == 2, "no pick: the first display switched on");
    check(geode_stage_pick_display(ids, flags, 4, 2) == 1, "the user's pick wins, even switched off");
    check(geode_stage_pick_display(ids, flags, 4, 7) == 2, "a pick the app is on falls back");
    check(geode_stage_pick_display(ids, flags, 4, 0) == 2, "a pick of the built-in screen falls back");
    check(geode_stage_pick_display(ids, flags, 2, -1) == 1, "nothing on: the first that qualifies");
    check(geode_stage_pick_display(ids, flags, 1, -1) == -1, "only the phone: none");
    check(geode_stage_pick_display(ids, flags, 0, -1) == -1, "no displays: none");
    check(geode_stage_pick_display(nullptr, nullptr, 4, -1) == -1, "null arrays: none");
    const unsigned refused[] = {flags[0], flags[1], ok | failed(3), flags[3]};
    check(geode_stage_pick_display(ids, refused, 4, 5) == 1, "a display that keeps refusing gives way");

    for (unsigned f = 0; f < 8; ++f) {
        const bool all = f == (GEODE_STAGE_PRESENT_WANTED | GEODE_STAGE_PRESENT_ENTITLED | GEODE_STAGE_PRESENT_FOUND);
        check(geode_stage_presents(f) == (all ? 1 : 0), "presents only when wanted, entitled and found");
    }
}

// The reason a settings page names agrees with the mode on every combination, and each reason is the first
// that holds: the entitlement before reduced motion, reduced motion before battery saver and silence.
void reasonNamesTheMode() {
    constexpr int kNone = 0, kHidden = 1, kEntitlement = 2, kReduced = 3, kSaver = 4, kSilence = 5;
    for (const Entitlement& e : kEntitlements) {
        for (unsigned f = 0; f <= 0x3FFu; ++f) {
            const int m = geode_surface_run_mode(f, e.until, kWall);
            const int r = geode_surface_reason(f, e.until, kWall);
            checkAt((r == kNone) == (m == kAnimated), "no reason exactly when moving", f, e.until);
            checkAt((r == kHidden) == (m == kStopped), "hidden exactly when stopped", f, e.until);
            if (m == kDark) checkAt(r == kSilence, "dark only for silence", f, e.until);
        }
    }
    const unsigned rest = GEODE_SURFACE_SILENT | GEODE_SURFACE_BATTERY_SAVER;
    check(geode_surface_reason(kOnScreen | kDefaults | GEODE_SURFACE_REDUCED_MOTION, kFree, kNow) == kEntitlement,
          "free: the entitlement first");
    check(geode_surface_reason(kOnScreen | kDefaults | rest | GEODE_SURFACE_REDUCED_MOTION, kPro, kNow) == kReduced,
          "reduced motion before battery saver and silence");
    check(geode_surface_reason(kOnScreen | kDefaults | rest, kPro, kNow) == kSaver, "battery saver before silence");
    check(geode_surface_reason(kOnScreen | kDefaults | GEODE_SURFACE_SILENT, kPro, kNow) == kSilence, "a silent rest");
    check(geode_surface_reason(kOnScreen | GEODE_SURFACE_BATTERY_SAVER, kPro, kNow) == kNone, "saver off: moving");
}

int main() {
    connectedDisplay();
    priorityOrder();
    preview();
    listening();
    trialEnd();
    decisionTableEveryCombination();
    restingNeverMakesItLivelier();
    listeningOverEveryCombination();
    trialEndsAtTheExactMillisecond();
    restateLandsJustAfterEveryTrialEnd();
    silenceRestsAfterSixSecondsAndSoundWakesIt();
    whatCountsAsSound();
    captureQuietAfterFourSecondsOfZeros();
    trialEndsDuringASilentRest();
    reducedMotion();
    dreamGoesDarkInSilence();
    settle();
    fps();
    stage();
    reasonNamesTheMode();
    if (failures == 0) std::printf("surface_run_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
