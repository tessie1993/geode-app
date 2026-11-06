// Host-side checks for live input's state machine (core/audio/LiveInput) through the C ABI the shell calls.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml); no device.
//
// What is pinned: turning on pauses the music and starts the capture; music starting from anywhere turns it
// off, also while paused in the background, so it never comes back over the music; the background pause and
// resume; and #367: the session's poll during the background pause is not a lost mic.
#include <cstdio>

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

}  // namespace

int main() {
    turningOnAndOff();
    musicWins();
    backgroundPause();
    lostCapture();
    badInput();
    if (failures == 0) std::printf("live_input_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
