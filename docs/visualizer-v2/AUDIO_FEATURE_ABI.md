# Sound input ABI — the ring, the cursor and the sound frame

How audio reaches the visuals, and the shape of what the visuals hear. The first version of this
file was written for the V2 plan's analysis layer (`core/analysis`, `OfflineAnalyzer`,
`ReactiveAnalyzer`, a 62.5 Hz analysis loop, `Gap` reads, chroma, `stereoCorrelation`). All of
that is gone. The sound engine (`core/sound`) replaced it, and this file now describes that.

Sources of truth, if this file and the code disagree — the code wins:

| What | Where |
|---|---|
| The ring | `engine/audio-core/.../audio/SampleRing.kt` |
| A surface's feed | `SoundInput.kt`, `SoundCursor.kt` (same package) |
| The engine | `core/sound/SoundEngine.hpp` |
| The frame | `core/sound/SoundFrame.h` (`GeodeSoundFrame`); Kotlin offsets in `SoundFrameLayout.kt` |
| The user's tuning | `GeodeSoundTuning` in the same header; Kotlin `SoundTuning.kt` (Settings › Audio) |
| The chain order | [`docs/AUDIO_CHAIN.md`](../AUDIO_CHAIN.md) |

---

## 1. The ring

`SampleRing` holds planar float PCM, two channels, 65,536 frames (`PlaybackSession.sampleRing`).
One writer at a time: the player's `PcmTap`, the mic, or another app's capture, all through
`PlaybackSession.captureSink`. A mono source fills channel 0 and leaves channel 1 silent; the ring
remembers the source's channel count.

Frames are counted from the start of an **epoch**. `beginEpoch()` sets the count back to 0 and
bumps `epoch`. The tap's boundary listener calls it on every flush (seek, track change, speed or
skip-silence change, end of stream), and `CaptureController` calls it when the source changes.
A frame index means something only with its epoch.

`oldestAvailable` is the oldest frame a reader may still copy while the writer runs. It keeps one
write's worth of runway (`maxWriteFrames`) clear of the writer.

There is no second ring. The raw PCM that MilkDrop (projectM) and the PCM scenes take is the mono mix
of exactly the samples a renderer's sound engine heard since its last frame: `Renderer::writeAudio`
hands them to `core/viz/PcmHandoff` as well, so every surface (live, cast, wallpaper, dream) feeds
projectM natively, and a frame with no new samples gives it nothing (#360).

## 2. One cursor per surface

`SoundInput` is the ring plus the rate of whatever writes it now (`PlaybackSession.sampleRateHz`:
the tap's format, or the capture's rate). It is published as `AudioBus.soundInput`, so the screen,
the live wallpaper and the dream all read the same one.

Each `VisualizerRenderer` reads it on its own GL thread through its own `SoundCursor`. The cursor
hands out every frame exactly once, in order, because the engine's filter bank runs per sample: a
skipped stretch would read as a hole in the drums, a repeated one as a double hit.

- A new epoch, or a cursor ahead of the writer, restarts it. `take()` then reports `restarted`,
  and the renderer resets the native engine (`vizResetAudio`) before it writes.
- After a stall longer than the ring holds, it skips to `oldestAvailable`.
- One take is at most `maxWriteFrames`; a longer stall keeps the newest audio.
- A mono source is handed over twice, so it reads centred and narrow, not hard left.

The renderer writes the take into the native renderer (`vizWriteAudio`). There is no analysis
thread and no clock of its own: `Renderer::beginFrame` steps the engine once per rendered frame
with that frame's dt.

**Export** does the same with the track instead of the ring. `render/offscreen/TrackAudio` decodes
the exported range (MediaExtractor and MediaCodec, or `AiffPcm`) and hands
`OffscreenSceneRenderer` exactly the audio up to each video frame; its own engine steps at
dt = 1/fps. Same code, same audio (the tap is ahead of any user DSP), so export equals live.

## 3. The engine

`core/sound/SoundEngine` runs MilkDrop's path and TouchDesigner's side by side:

- `write()` puts samples in the engine's own ring and runs each one through a filter bank
  (4-pole bands with a power envelope).
- `step(dt)` takes the newest window (about 40 ms: an FFT of 2048 at 44.1/48 kHz, 4096 at
  88.2/96, 8192 at 176.4/192), applies Hann, FFT (`core/sound/Fft`, its own radix-2) and a tilt, and sums the bands
  against their own running means. It also reduces the filter bank's slice since the last frame
  to one level per band, through lag, threshold and gain, and into the drum triggers.

Every stage scales its smoothing by dt. One engine per surface, never shared between threads.

## 4. The sound frame

`GeodeSoundFrame` is plain C and all floats, so it crosses JNI as one float array in declaration
order: 74 scalars, then 16 EQ bands, 64 spectrum points and 128 waveform points
(`GEODE_SOUND_FRAME_FLOATS`). The header documents each value. The kinds:

| Kind | Values |
|---|---|
| Relative levels (1.0 is typical for this song now) | bass, mid, treble, vol, sub, midHigh and their `*Att`; flux; the EQ bands |
| Shaped, 0..1 | low, middle, high, level; the presences, intensity, presence; loudness, centroid, rolloff, flatness, densities, energyTrend, peakFreq, peakMag; drum and onset envelopes, toggles, randoms; the melody values |
| Clocks, only forward | bassTime, midTime, trebleTime, curvedTime, hitTime, noteTime |
| Triggers, > 0 on one frame | kick, snare, hat, kick2…kick16, snare2, snare4, hardCut, onset, silence, returned, surge, noteChange, legato, reattack |
| Stereo | stereoWidth (0 mono, 1 out of phase), stereoPan (-1 … 1) |
| Arrays | `eq[16]`, `spectrum[64]` (log, 20 Hz–20 kHz), `waveform[128]` (the newest ~13 ms) |

`SoundFrameLayoutTest` parses the header and fails when a field is added, dropped or moved
without `SoundFrameLayout` following. The reaction matrix reads the frame through
`ReactSource` (`core/viz/Reactions.hpp`: the frame's scalars and the 16 EQ bands). Styles read
it through `lib_sound` (`uSound`, `uSoundTex`), uploaded by `core/viz/SoundUniforms`.

## 5. Presentation time

Superseded (#482): `AudioPresentationClock`, `ClockSegment` and `SinkClockDriver` are deleted.
The native playout clock, `core/sound/interp/PlayoutClock` (`geode_playout_*`), maps the sink's
media-time position (from `AudioTrack.getTimestamp`, so output and Bluetooth latency included,
speed and skipped silence already counted) through anchors taken from the buffers the sink
handles to the ring sample the speaker plays now. The history below is kept for the record.

The tap sits before silence skipping and Sonic, so a ring frame index is not the time the
listener hears it: speed scales the mapping and skipped silence removes spans from it.
`AudioPresentationClock` kept that mapping as a list of `ClockSegment`s (epoch, discontinuity
generation, first input frame, presentation start, slope, speed, skipped frames).
`SinkClockDriver` appended a segment at each tap boundary, from the chain's speed and
skip-silence hooks, all on the playback thread.

Two limits, recorded when the driver was written:

- The absolute offset is unmeasured: there is no term for the sink's own output latency.
- Media3 raises no event for skipped silence, so the driver samples the stage's counter at each
  boundary. Every segment carries `skippedInputSamples = 0`; span placement is unknown.

The visuals do not read presentation time today. The sound engine hears the ring as it fills.
