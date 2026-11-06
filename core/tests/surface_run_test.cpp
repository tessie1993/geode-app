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

int main() {
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
    trialEndsDuringASilentRest();
    reducedMotion();
    settle();
    fps();
    if (failures == 0) std::printf("surface_run_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
