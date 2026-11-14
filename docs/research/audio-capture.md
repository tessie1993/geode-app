# Audio from other apps: what breaks, why, and what changed

One fact per line. Read against the code at the commit that added this file. Nothing here was run on a device; every "verify" line is for one.

## Sources

- AV capture guide (who can be captured, policy, usages): https://developer.android.com/media/platform/av-capture
- `AudioPlaybackCaptureConfiguration` (usages MEDIA, GAME, UNKNOWN only; policy; same profile): https://developer.android.com/reference/android/media/AudioPlaybackCaptureConfiguration
- `AudioPlaybackCaptureConfiguration.Builder` (`excludeUid` with `addMatchingUsage` is allowed; usage+usage-exclude and uid+uid-exclude are not): https://developer.android.com/reference/android/media/AudioPlaybackCaptureConfiguration.Builder
- MediaProjection (Android 14 single-use token, FGS type, app screen sharing, Android 15 QPR1 stop on lock): https://developer.android.com/media/grow/media-projection
- Android 16 behaviour changes (nothing for projection or capture): https://developer.android.com/about/versions/16/behavior-changes-16
- AAudio NDK reference (input presets; no MediaProjection, no playback capture): https://developer.android.com/ndk/reference/group/audio
- `Visualizer` (session 0 = output mix, 8-bit, `MODIFY_AUDIO_SETTINGS`): https://developer.android.com/reference/android/media/audiofx/Visualizer
- `AudioManager.isMusicActive`: https://developer.android.com/reference/android/media/AudioManager#isMusicActive()
- `Process.THREAD_PRIORITY_URGENT_AUDIO`: https://developer.android.com/reference/android/os/Process#THREAD_PRIORITY_URGENT_AUDIO
- `AudioRecord.read(ByteBuffer, int, int)`: https://developer.android.com/reference/android/media/AudioRecord#read(java.nio.ByteBuffer,%20int,%20int)

## Platform facts that bound every fix

- Only a player with usage MEDIA, GAME or UNKNOWN can be captured; every other usage cannot (`AudioPlaybackCaptureConfiguration`).
- The playing app decides: `allowAudioPlaybackCapture="false"`, `setAllowedCapturePolicy(ALLOW_CAPTURE_BY_SYSTEM|NONE)` or a per-player policy blocks capture, the most restrictive wins (av-capture).
- A blocked app is handed to the capture as digital silence; no setting on either side changes it (av-capture).
- Apps targeting API 29+ allow capture by default; which shipping apps opt out is not documented anywhere official (searched; none found).
- Spotify, YouTube and Chrome policies were not confirmed from a primary source; verify with `adb shell dumpsys audio` (its playback-capture section lists each uid's allowed policy) while each plays.
- There is no NDK path to another app's playback: AAudio has no MediaProjection input and its only capture-like presets (`SYSTEM_ECHO_REFERENCE`, `SYSTEM_HOTWORD`) need privileged permissions (AAudio reference).
- So Oboe cannot capture other apps; the capture stays a Java `AudioRecord` built with `setAudioPlaybackCaptureConfig`. No Oboe path is faked.
- Reading that `AudioRecord` from a native thread would still be a JNI call into Java per read; it saves nothing, so it was not done.
- Android 14+: one `getMediaProjection` per consent intent, FGS type `mediaProjection` started first; `PlaybackCaptureService.kt:262-288` does both in that order.
- Android 15 QPR1+: the projection stops when the screen locks; `PlaybackCaptureService.kt:240-244` catches `onStop`, and the capture switches off rather than reading silence.
- Android 14 QPR2+ consent offers "one app" or "entire screen"; the docs describe the choice for video only and say nothing of audio (open question below).

## Symptom: Spotify, visuals freeze until the screen is tapped

- A capture that hears digital zero gives the engine silence, and silence holds the picture still by design: `core/sound/DriveGain.hpp:37` (`motionRate` from presence) and `core/viz/RendererFrame.cpp` (`musicRate_`).
- A tap moves the picture through the touch field, not through sound; so "frozen until tapped" is what a refused capture looks like.
- Most likely cause: Spotify does not let other apps capture it (platform limit; verify with `dumpsys audio`).
- The app could only say so with notification access: `refusedByApp` needed the other app's media session (`CaptureController.kt`, old line 275); without it the Player showed the vaguer "hearing silence".
- Fix: the refusal is now native (`core/sound/Silence.hpp` `captureRefused`) and takes a second witness, `AudioManager.isMusicActive`, which needs no access; the Player and Settings then name the refusal and offer the microphone.
- Fix: the capture thread was a default-priority JVM thread (`AudioCapturePump.kt`, `thread {}`), starved in the background where the user is while in Spotify; it now runs at `THREAD_PRIORITY_URGENT_AUDIO` with one reused direct buffer.
- Ruled out by reading: the stage's run rule (`core/viz/StageRun.hpp`) is visibility only, and the capture's listen limit is the newest sample (`core/sound/interp/SpeakerSync.hpp:38`).
- Verify: with Spotify playing, the Player's subtitle should now read "refused" within about 5 s (4 s quiet plus one 500 ms poll).

## Symptom: YouTube and the browser do not work

- Same mechanism as Spotify if those apps (or that content) refuse capture; both go silent and still.
- Not confirmed: YouTube and Chrome policies are not published; verify each with `dumpsys audio` and with a plain web page playing audio in Chrome.
- If `dumpsys` shows them allowed but the capture is silent, look at how they play (offload or a direct output); the docs do not say whether a capture hears those streams (open question).
- Fix made: honest naming through `captureRefused` (above); nothing else is possible from this side for an app that refuses.

## Symptom: own app playback is slow, with audio lag

- Root cause in the hand-over: giving the player's ring to a capture took three separate Kotlin calls (`PlayerSession.kt:121-126`: `tap.owns`, `pcmRingBeginEpoch`, `soundSetPlayerSource`), with the decision in Kotlin.
- Fix: one native call, `geode_pcm_ring_hand_over` (`core/api/geode_ingest_api.cpp`): the tap shut out, a new epoch, the engines' player-source flag, in that order; the tap's writes and flushes ask the ring (`geode_pcm_ring_write_tap`, `_tap_flush`), so `PcmTap.owns` is gone.
- Fix: the capture now excludes this app's own uid (`PlaybackCapture.kt`, `excludeUid(Process.myUid())`), so between the own player starting and `onIsPlayingChanged` stopping the capture the visuals never hear our own music twice, the second time late through the capture.
- Not fixed, found: `PlayoutSink.kt:29-32` measures the tap's writes as the ring head's change around `super.handleBuffer`; if a capture writes the ring at the same moment (a paused player still filling its AudioTrack), the playout clock gets anchors that point at the capture's samples. It is brief (until the sink's buffer drains) and needs a per-writer count in the ring to fix; left for a follow-up.
- Not attributable to the capture path by reading: sustained slowness while playing own music with no capture on; render load and heat belong to the thermal work happening elsewhere.
- Verify: own playback with capture never turned on vs after turning it on and off; Settings › Picture delay unchanged; compare the lag.

## Symptom: "entire phone as sound source" does not work at all

- Reading 1, the wallpaper's and screen saver's "React to any app": it is a `Visualizer` on audio session 0 (`wallpaper/OutputMixCapture.kt`), the only capture a wallpaper can hold (no consent dialog, no FGS).
- Session 0 sees only the output mix it is attached to: 8-bit, about 20 snapshots a second, and with `SCALING_MODE_AS_PLAYED` scaled by the volume (`Visualizer` reference).
- Many devices hand it nothing for streams on other outputs (offload, deep buffer, direct); the docs do not promise more, so this is a platform limit on those devices.
- The app only said "this phone doesn't share its sound" when the Visualizer failed to open (`SoundSource.kt:118`); one that opened and heard only zeros failed silently.
- Fix: `OutputMixCapture` now tracks digital zero natively (`captureWriteBytes`, `geode_capture_write`), and `SoundSource` saves "mix refused" when it hears only zeros while `isMusicActive` (native `captureRefused`), so the options page says so.
- Reading 2, the consent dialog's "Entire screen": the code captures the whole device's audio either way; whether "One app" narrows the audio to that app is not documented (open question).
- Verify: on Android 14 QPR2+, start "Other apps" once with "Entire screen" and once with "One app", play YouTube, and compare.

## The audio-in path: how much JVM is left (owner's ask)

- Mic: already a native Oboe input stream (`core/audio/ingest/MicInput`, `android/OboeMic.cpp`); no JVM thread, the callback writes the ring; `MicCapture.kt` no longer exists.
- Mic: now asks for `PerformanceMode::LowLatency`, `SharingMode::Exclusive` then `Shared`; presets stay UNPROCESSED else VOICE_RECOGNITION; disconnects reopen under `ReopenBudget` (3 in 10 s), host-tested in `mic_input_test.cpp`.
- Mic, not done: leaving the rate unset. The plan's first rate is already the device's own (`CapturePlan`), and `OboeMic` refuses a stream whose rate differs from the request; taking whatever rate Oboe opens needs `MicInput` to adopt the stream's rate. Blocked only by that change, not by the platform.
- Mic, not done: an exclusive stream that opens with the wrong format is dropped rather than retried shared; the plan then tries its next format.
- Playback capture: stays a Java `AudioRecord` (no NDK API, above); its loop is now platform glue only: one direct `ByteBuffer` per run sized by native (`geode_capture_read_bytes`), `READ_BLOCKING`, one JNI call per read (`captureWriteDirect`) that writes, measures and returns the heard time; no Java array, no copy, no per-read allocation.
- Playback capture, not done: the AudioRecord buffer is still four platform minimums (`CapturePlan::kCaptureBufferMinimums`); a smaller one is lower latency but risks overruns; tune on a device.
- Tap (`PcmTap.kt`): a Media3 Java callback, so it stays Kotlin; it already hands the direct buffer where it lies, and its ownership is now native.
- Output mix (`OutputMixCapture.kt`): the `Visualizer` listener is a Java callback with a Java array; one critical-pinned JNI call per snapshot; must stay.
- `SoundSource.kt`: the source rule is native (`core/sound/SoundSource.hpp`); Kotlin maps the answer to a ring handle.
- `AudioBus.soundInput`: a process global read by `Stage.doFrame`, which calls `stageSetSound` only when the handle changes; a native registry would remove it, left as is (no bug found).
- `PlayoutSink.kt`: a Media3 sink wrapper, must stay Kotlin; its per-writer issue is above.

## Kotlin logic ported to C++ (rule 5)

- The refusal decision, `active && quiet && playing` in `CaptureController.refreshExternalAudio`, is now `core/sound/Silence.hpp` `captureRefused`; host test `surface_run_test.cpp`.
- Which other app to name, `maxByOrNull { playing }` in `NowPlayingBridge.current`, is now `core/sound/NowPlaying.hpp` `pick`; host test `surface_run_test.cpp`.
- The capture's peak and heard-at per read, two JNI calls and a Kotlin choice of array type, is now `geode_capture_write`; host test `pcm_ring_test.cpp`.
- One read's byte size (encoding width times channels times frames) is now `CapturePlan::captureReadBytes`; host test `capture_plan_test.cpp`.
- The ring hand-over (tap flag, epoch, player source) is now `geode_pcm_ring_hand_over`; host test `pcm_ring_test.cpp`.

## Open questions for a device

- Does an Android 14 QPR2+ "One app" projection limit `AudioPlaybackCapture` to that app's uid, or pause it while that app is hidden?
- Do Spotify, YouTube and Chrome set a capture policy other than ALLOW_CAPTURE_BY_ALL (`dumpsys audio`)?
- Does the capture hear offloaded or direct-output playback on the test phone?
- Is `THREAD_PRIORITY_URGENT_AUDIO` granted to the capture thread (`ps -T -p <pid> -o tid,ni,comm` shows nice -19)? It falls back to the default with a logged warning if refused.
