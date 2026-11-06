// Host-side checks for live input's state machine (core/audio/LiveInput) through the C ABI the shell calls.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml); no device.
//
// What is pinned: turning on pauses the music and starts the capture; music starting from anywhere turns it
// off, also while paused in the background, so it never comes back over the music; the background pause and
// resume; and #367: the session's poll during the background pause is not a lost mic.
//
// Also pinned (PR #444 mutation pass): every way out of on hands the ring back to the player; only turning on
// pauses the music; the same event twice does nothing more; a real loss does not resume on return; music while
// away wins in either order; any number of polls during the pause; and seeded histories against a simulated
// platform, where the capture runs exactly while the state is on.
#include <cstdio>
#include <initializer_list>
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

enum { kOff = 0, kOn = 1, kPaused = 2, kLost = 3 };
enum { kTurnOn = 0, kTurnOff = 1, kStartFailed = 2, kMusicStarts = 3, kBackground = 4, kForeground = 5, kCaptureStopped = 6 };

struct Result {
    int state;
    unsigned actions;
};

Result step(int state, int event, bool permission = true) {
    unsigned actions = 0xffffffffu;
    const int next = geode_live_input_step(state, event, permission ? 1 : 0, &actions);
    return {next, actions};
}

bool has(unsigned actions, unsigned bit) { return (actions & bit) != 0; }

void turningOnAndOff() {
    const Result on = step(kOff, kTurnOn);
    check(on.state == kOn, "turn on: on");
    check(has(on.actions, GEODE_LIVE_INPUT_ACT_PAUSE_MUSIC), "turn on pauses the music");
    check(has(on.actions, GEODE_LIVE_INPUT_ACT_START_CAPTURE), "turn on starts the capture");
    check(has(on.actions, GEODE_LIVE_INPUT_ACT_STOP_OTHER_APP_AUDIO), "turn on stops another app's audio");
    check(step(kOn, kTurnOn).actions == 0, "turning on while on does nothing");

    const Result failed = step(kOn, kStartFailed);
    check(failed.state == kOff, "a failed start leaves it off");

    const Result off = step(kOn, kTurnOff);
    check(off.state == kOff && has(off.actions, GEODE_LIVE_INPUT_ACT_STOP_CAPTURE), "turn off stops the capture");
    check(has(off.actions, GEODE_LIVE_INPUT_ACT_PLAYER_RATE), "turn off hands the ring back to the player's rate");
}

void musicWins() {
    check(step(kOn, kMusicStarts).state == kOff, "music starting turns it off");
    check(step(kPaused, kMusicStarts).state == kOff, "music started while away turns it off for good");
    check(step(kOff, kMusicStarts).actions == 0, "music starting while off does nothing");
    check(geode_live_input_is_on(kPaused) == 1, "the background pause counts as on");
    check(geode_live_input_is_on(kLost) == 0 && geode_live_input_is_on(kOff) == 0, "off and lost are off");
}

void backgroundPause() {
    const Result away = step(kOn, kBackground);
    check(away.state == kPaused && away.actions == GEODE_LIVE_INPUT_ACT_STOP_CAPTURE, "background releases the capture");
    check(step(kOff, kBackground).actions == 0, "background while off does nothing");

    // #367: the poll keeps running in the background and finds the capture stopped.
    const Result poll = step(kPaused, kCaptureStopped);
    check(poll.state == kPaused && poll.actions == 0, "a poll during the background pause changes nothing");

    const Result back = step(kPaused, kForeground);
    check(back.state == kOn && has(back.actions, GEODE_LIVE_INPUT_ACT_START_CAPTURE), "foreground resumes it");
    check(!has(back.actions, GEODE_LIVE_INPUT_ACT_PAUSE_MUSIC), "resuming never pauses music");
    check(step(kPaused, kForeground, false).state == kLost, "foreground without the permission: lost");
    check(step(kOn, kForeground).actions == 0, "foreground while on does nothing");
}

void lostCapture() {
    check(step(kOn, kCaptureStopped).state == kLost, "a capture that stops by itself while on is lost");
    check(step(kOff, kCaptureStopped).actions == 0, "the poll while off does nothing");
    check(step(kLost, kTurnOn).state == kOn, "a lost mic can be turned on again");
}

void badInput() {
    unsigned actions = 7;
    check(geode_live_input_step(9, kTurnOn, 1, &actions) == 9 && actions == 0, "an unknown state is left alone");
    check(geode_live_input_step(kOn, 42, 1, &actions) == kOn && actions == 0, "an unknown event is ignored");
}

// Expected values below come from LiveInput.hpp's rule list and the deleted Kotlin (`if (micActive)
// setMicEnabled(false)` on every music start), never from recomputing the table.

constexpr int kStates = 4;
constexpr int kEvents = 7;
constexpr unsigned kStopOther = GEODE_LIVE_INPUT_ACT_STOP_OTHER_APP_AUDIO;
constexpr unsigned kPause = GEODE_LIVE_INPUT_ACT_PAUSE_MUSIC;
constexpr unsigned kStart = GEODE_LIVE_INPUT_ACT_START_CAPTURE;
constexpr unsigned kStop = GEODE_LIVE_INPUT_ACT_STOP_CAPTURE;
constexpr unsigned kReset = GEODE_LIVE_INPUT_ACT_RESET_SOUND;
constexpr unsigned kRate = GEODE_LIVE_INPUT_ACT_PLAYER_RATE;

void checkf(bool ok, const char* what, int state, int event, int extra) {
    if (ok) return;
    std::printf("FAIL: %s (state %d, event %d, case %d)\n", what, state, event, extra);
    ++failures;
}

// Kills: MusicStarts -> {Off, 0} (the capture keeps running behind an "off" switch); kPlayerRate or kResetSound
// dropped from StartFailed, CaptureStopped, turnOff or the foreground-without-permission row.
void leavingOnHandsTheSoundBack() {
    for (int s : {static_cast<int>(kOn), static_cast<int>(kPaused)}) {
        for (int e = 0; e < kEvents; ++e) {
            for (int p = 0; p <= 1; ++p) {
                const Result r = step(s, e, p != 0);
                if (r.state != kOff && r.state != kLost) continue;
                checkf(has(r.actions, kRate), "leaving on gives the ring back the player's rate", s, e, p);
                checkf(has(r.actions, kReset), "leaving on starts a fresh sound epoch", s, e, p);
                checkf(!has(r.actions, kStart), "leaving on never starts a capture", s, e, p);
            }
        }
    }
    check(has(step(kOn, kMusicStarts).actions, kStop), "music starting while on stops the capture");
}

// Kills: kResetSound dropped from turnOn() or from the foreground resume (the old source's epoch carries into the
// mic's samples); TurnOn while on not staying on.
void comingOnStartsAFreshSource() {
    for (int s = 0; s < kStates; ++s) {
        for (int e = 0; e < kEvents; ++e) {
            const Result r = step(s, e, true);
            if (r.state != kOn || s == kOn) continue;
            checkf(has(r.actions, kStart), "coming on starts the capture", s, e, 1);
            checkf(has(r.actions, kReset), "coming on starts a fresh sound epoch", s, e, 1);
        }
    }
    check(step(kOn, kTurnOn).state == kOn, "turning on while on stays on");
    check(geode_live_input_is_on(kOn) == 1, "on counts as on");
}

// Kills: kPauseMusic or kStopOtherAppAudio on any row but TurnOn, the resume above all (the owner's rule: the
// resume never pauses music the person started meanwhile).
void onlyTurningOnPausesTheMusic() {
    for (int s = 0; s < kStates; ++s) {
        for (int e = 0; e < kEvents; ++e) {
            for (int p = 0; p <= 1; ++p) {
                if (e == kTurnOn) continue;
                const Result r = step(s, e, p != 0);
                checkf(!has(r.actions, kPause), "only turning on pauses the music", s, e, p);
                checkf(!has(r.actions, kStopOther), "only turning on stops another app's audio", s, e, p);
            }
        }
    }
}

// Kills: AppBackground on isOn(s) (a second onStop stops again), CaptureStopped on s != Off (every 500 ms poll on
// a lost mic resets the sound), MusicStarts on s != Off, AppForeground on s != On. The platform repeats these
// (lifecycle callbacks, the poll, music starting from several places at once); the second one changes nothing.
void repeatedEventsChangeNothing() {
    for (int s = 0; s < kStates; ++s) {
        for (int e = 0; e < kEvents; ++e) {
            for (int p = 0; p <= 1; ++p) {
                const Result once = step(s, e, p != 0);
                const Result twice = step(once.state, e, p != 0);
                checkf(twice.state == once.state, "the same event twice lands where once does", s, e, p);
                const bool quiet = e == kTurnOn || e == kMusicStarts || e == kBackground || e == kForeground ||
                                   e == kCaptureStopped;
                if (quiet) checkf(twice.actions == 0, "the same event again does nothing", s, e, p);
            }
        }
    }
}

// Kills: AppForeground resuming from Lost or Off (a real loss coming back by itself), AppBackground taking Lost
// into the pause, MusicStarts acting on Lost. Only the app's own background pause resumes on return.
void aRealLossDoesNotResume() {
    Result r = step(kOn, kCaptureStopped);
    r = step(r.state, kBackground);
    check(r.state == kLost && r.actions == 0, "leaving the screen with a lost mic does nothing");
    r = step(r.state, kForeground);
    check(r.state == kLost && !has(r.actions, kStart), "coming back to a lost mic does not resume it");

    const Result offBack = step(kOff, kForeground);
    check(offBack.state == kOff && offBack.actions == 0, "coming back with live input off does nothing");
    const Result lostMusic = step(kLost, kMusicStarts);
    check(lostMusic.state == kLost && lostMusic.actions == 0, "music over a lost mic does nothing (old micActive guard)");
    check(has(step(kLost, kTurnOn).actions, kStart), "turning a lost mic on again starts the capture");
}

// Kills: the foreground-without-permission row also asking for kStartCapture. Context7 (/websites/developer_android,
// one-time permissions): revoking a permission terminates the process, so this row is defensive.
void foregroundWithoutPermissionNeverStarts() {
    const Result r = step(kPaused, kForeground, false);
    check(!has(r.actions, kStart), "back without the permission never starts the capture");
}

// Kills: MusicStarts while paused staying paused, AppForeground resuming from Off. Music started from the
// notification, a headset or the car while away wins for good, whichever the platform delivers first.
void musicWhileAwayWinsInEitherOrder() {
    Result a = step(kOn, kBackground);
    a = step(a.state, kMusicStarts);
    Result b = step(kOn, kMusicStarts);
    b = step(b.state, kBackground);
    check(a.state == kOff && b.state == kOff, "away then music, or music then away: off");
    for (int s : {a.state, b.state}) {
        const Result back = step(s, kForeground);
        check(back.state == kOff && !has(back.actions, kStart) && !has(back.actions, kPause),
              "the mic never comes back over the music");
    }
    Result off = step(kPaused, kTurnOff);
    off = step(off.state, kForeground);
    check(off.state == kOff && off.actions == 0, "turned off while away stays off on return");
}

// Kills: CaptureStopped on isOn(s) (#367). Any number of polls during the pause ends the same as none.
void pollsDuringThePauseDoNotMatter() {
    for (int polls = 0; polls <= 6; ++polls) {
        Result r = step(kOn, kBackground);
        for (int i = 0; i < polls; ++i) r = step(r.state, kCaptureStopped);
        r = step(r.state, kForeground);
        checkf(r.state == kOn && has(r.actions, kStart), "the pause resumes after any number of polls", kPaused,
               kCaptureStopped, polls);
    }
}

// Model check over seeded random histories (raw mt19937 output, no distributions). A simulated platform carries
// out the actions: a start succeeds only with the permission (MicCapture.start) and sometimes fails anyway; the mic
// can die while capturing (pre-Android 10 contention, a USB mic unplugged); the poll reports only a capture that is
// not reading, as CaptureController.refreshMicState does. The permission is fixed per history (Context7, one-time
// permissions: revoking it terminates the process). Invariants: the capture runs exactly while the state is on, and
// only turning on pauses the music. Kills any row that leaves a capture running in Off, Lost or the pause, or that
// says on with no capture asked for.
void historiesKeepTheCaptureAndTheStateTogether() {
    static const int kShellEvents[6] = {kTurnOn, kTurnOff, kMusicStarts, kBackground, kForeground, kCaptureStopped};
    std::mt19937 rng(367u);
    for (int history = 0; history < 400; ++history) {
        const bool permission = (rng() & 3u) != 0u;
        int state = kOff;
        bool capturing = false;
        for (int t = 0; t < 60; ++t) {
            const unsigned pick = static_cast<unsigned>(rng() % 7u);
            int event = kCaptureStopped;
            if (pick == 6u) {
                capturing = false;  // the mic dies by itself; the next poll sees it
            } else {
                event = kShellEvents[pick];
                if (event == kCaptureStopped && capturing) continue;  // the poll is silent while reading
            }
            unsigned actions = 0;
            state = geode_live_input_step(state, event, permission ? 1 : 0, &actions);
            if (event != kTurnOn) checkf(!has(actions, kPause), "only turning on pauses the music", state, event, history);
            if (has(actions, kStart)) {
                const bool started = permission && (rng() % 10u) != 0u;
                capturing = started;
                if (!started) state = geode_live_input_step(state, kStartFailed, permission ? 1 : 0, &actions);
            }
            if (has(actions, kStop)) capturing = false;
            checkf(state >= kOff && state <= kLost, "the state stays a known one", state, event, history);
            checkf(capturing == (state == kOn), "the capture runs exactly while on", state, event, history);
        }
    }
}

// Kills: dropping the state < 0 range check (a negative state gets stepped), dropping the actions_out null check.
void edgesOfTheAbi() {
    unsigned actions = 7;
    check(geode_live_input_step(-1, kTurnOn, 1, &actions) == -1 && actions == 0, "a negative state is left alone");
    actions = 7;
    check(geode_live_input_step(kOff, -1, 1, &actions) == kOff && actions == 0, "a negative event is ignored");
    check(geode_live_input_step(kOff, kTurnOn, 1, nullptr) == kOn, "no actions pointer still steps");
    check(geode_live_input_step(kOn, kBackground, 1, nullptr) == kPaused, "no actions pointer: background pauses");
}

}  // namespace

int main() {
    turningOnAndOff();
    musicWins();
    backgroundPause();
    lostCapture();
    badInput();
    leavingOnHandsTheSoundBack();
    comingOnStartsAFreshSource();
    onlyTurningOnPausesTheMusic();
    repeatedEventsChangeNothing();
    aRealLossDoesNotResume();
    foregroundWithoutPermissionNeverStarts();
    musicWhileAwayWinsInEitherOrder();
    pollsDuringThePauseDoNotMatter();
    historiesKeepTheCaptureAndTheStateTogether();
    edgesOfTheAbi();
    if (failures == 0) std::printf("live_input_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
