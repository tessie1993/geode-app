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

// What the player does when a track will not play. SkipToNext: another track follows and fewer than
// kMaxConsecutiveFailures have failed in a row. StopEndOfQueue: nothing follows. StopSourceUnavailable: that
// many in a row failed (the storage or the folder permission is probably gone); checked first.
inline constexpr int kMaxConsecutiveFailures = 3;
enum class ErrorAction : int { SkipToNext = 0, StopEndOfQueue = 1, StopSourceUnavailable = 2 };
ErrorAction errorAction(int consecutiveFailures, bool hasNext);

}  // namespace geode::audio
