#pragma once

// Settings › Playback's rows as the player takes them: speed, pitch, the fades, the crossfade, the repeat
// mode and ReplayGain, with the range, step and default each one has. Pure; host-tested in
// core/tests/playback_controls_test.cpp.
namespace geode::audio {

// The saved options that have a range. Values travel as floats; the whole-number ones are exact in them.
enum PlaybackControl : int {
    kFadeMs = 0,
    kCrossfadeMs = 1,
    kRepeatMode = 2,
    kSpeed = 3,
    kPitchSemitones = 4,
    kReplayGainMode = 5,
    kReplayGainPreampDb = 6,
    kPlaybackControls = 7,
};

struct ControlRange {
    float min = 0.0f;
    float max = 0.0f;
    float step = 0.0f;  // what the row's slider moves by, 0 where it has none
    float fallback = 0.0f;
};

// All zero for a control that is not one.
ControlRange controlRange(int control);

// `value` held to the control's range; a value that is not a number becomes the control's default, a control
// that is not one leaves the value alone.
float clampControl(int control, float value);

// A crossfade slider position, in seconds, as milliseconds: whole seconds, Off below half a second, never
// past the range.
int crossfadeMsFromSeconds(float seconds);

// The pitch rate a shift of `semitones` plays at, as Media3's PlaybackParameters takes it: an
// equal-tempered semitone is a twelfth of an octave, so +12 doubles and -12 halves.
float pitchRatio(float semitones);

// A slider's value on its step: the nearest multiple of `step`, a half step rounding up (toward
// +infinity, so -0.25 on a 0.5 step is 0). With a step that is not a positive finite number, or a
// value that is not finite, the value is left as it is.
float snapToStep(float value, float step);

}  // namespace geode::audio
