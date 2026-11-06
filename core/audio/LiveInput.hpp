#pragma once

#include <cstdint>

// Live input (the microphone driving the visual): its on/off state and every decision about it.
//
// The shell owns only the Android parts (the AudioRecord, the permission, the player) and reports
// what happens as events; this machine says what state live input is in and which of those parts to
// act on. One table, so the rules cannot drift between the screens and callbacks that used to each
// decide a piece of it in Kotlin.
//
// The rules:
// - Turning it on stops another app's audio and pauses the music (the room is the source now), then
//   starts the capture; a start that fails leaves it off, and the shell shows why.
// - Music starting from anywhere (the screen, the notification, a headset, the car) turns it off:
//   music wins.
// - The app leaving the screen pauses it: Android gives a background app without a microphone
//   foreground service silence anyway, so the capture is released, not left reading zeros. Coming
//   back resumes it if the permission is still there, else it is lost.
// - The shell polls whether the capture is still reading. A capture that stopped by itself while on
//   is lost (another app took the mic, the device went away). The background pause is the app's own
//   choice, so a poll during it changes nothing (#367: the poll used to read it as a lost mic).
// - It always starts off: it is never restored after Android kills the app (owner, #367).
//
// Pure and stateless: the caller keeps the State and hands it back in.
namespace geode::audio::live_input {

enum class State : int {
    Off = 0,
    On = 1,
    PausedInBackground = 2,
    Lost = 3,  // was on, then the capture stopped on its own or access went; shown as unavailable
};

enum class Event : int {
    TurnOn = 0,
    TurnOff = 1,
    StartFailed = 2,      // the capture the machine asked for could not start
    MusicStarts = 3,
    AppBackground = 4,
    AppForeground = 5,
    CaptureStopped = 6,   // a poll found the capture not reading
};

// What the shell does, in this order. Values are the ABI (GEODE_LIVE_INPUT_* in geode_api.h).
enum Action : uint32_t {
    kStopOtherAppAudio = 1u << 0,
    kPauseMusic = 1u << 1,
    kStartCapture = 1u << 2,   // then report StartFailed if it fails
    kStopCapture = 1u << 3,
    kResetSound = 1u << 4,     // a new source: the sound engine starts a fresh epoch
    kPlayerRate = 1u << 5,     // the ring's rate goes back to the player's
};

struct Step {
    State next;
    uint32_t actions;
};

// Whether live input counts as on: on, or paused while the app is away. Counting the pause matters:
// music started from the notification while the app is away turns it off for good, so the mic does
// not come back over the music when the app does.
inline bool isOn(State s) { return s == State::On || s == State::PausedInBackground; }

inline Step turnOn() { return {State::On, kStopOtherAppAudio | kPauseMusic | kStartCapture | kResetSound}; }

inline Step turnOff() { return {State::Off, kStopCapture | kResetSound | kPlayerRate}; }

inline Step step(State s, Event e, bool hasPermission) {
    switch (e) {
        case Event::TurnOn:
            return s == State::On ? Step{s, 0} : turnOn();
        case Event::TurnOff:
            return turnOff();
        case Event::StartFailed:
            return {State::Off, kResetSound | kPlayerRate};
        case Event::MusicStarts:
            return isOn(s) ? turnOff() : Step{s, 0};
        case Event::AppBackground:
            return s == State::On ? Step{State::PausedInBackground, kStopCapture} : Step{s, 0};
        case Event::AppForeground:
            if (s != State::PausedInBackground) return {s, 0};
            // Back on screen: resume, but never pause music the user started meanwhile (music
            // starting already turned it off), and only while the permission is still there.
            if (!hasPermission) return {State::Lost, kResetSound | kPlayerRate};
            return {State::On, kStartCapture | kResetSound};
        case Event::CaptureStopped:
            return s == State::On ? Step{State::Lost, kResetSound | kPlayerRate} : Step{s, 0};
    }
    return {s, 0};
}

}  // namespace geode::audio::live_input
