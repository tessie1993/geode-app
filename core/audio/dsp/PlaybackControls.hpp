#pragma once

// Settings › Playback's speed and pitch rows as the player takes them. Pure; host-tested in
// core/tests/playback_controls_test.cpp.
namespace geode::audio {

// The pitch rate a shift of `semitones` plays at, as Media3's PlaybackParameters takes it: an
// equal-tempered semitone is a twelfth of an octave, so +12 doubles and -12 halves.
float pitchRatio(float semitones);

// A slider's value on its step: the nearest multiple of `step`, a half step rounding up (toward
// +infinity, so -0.25 on a 0.5 step is 0). With a step that is not a positive finite number, or a
// value that is not finite, the value is left as it is.
float snapToStep(float value, float step);

}  // namespace geode::audio
