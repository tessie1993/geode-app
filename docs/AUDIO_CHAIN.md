# The Audio Chain

Where playback audio goes, in what order, and which of those stages the visuals
can see. Everything here is already enforced somewhere in the tree; this
collects it so the next person to add an audio feature does not have to
reconstruct it from three files and a test.

Sources of truth, if this doc and the code ever disagree — the code wins:

| What | Where |
|---|---|
| The chain, and why the order is this order | `audio/dsp/MvzAudioProcessorChain.kt` |
| Where it is installed | `audio/TapRenderersFactory.kt` |
| The two rules | this document; the `AudioChainContractTest` that pinned them is not in the tree |
| Platform effects (a different mechanism) | `audio/AudioFxController.kt`, `ui/EqualizerSettings.kt` |

## The order

```
ExoPlayer
   │
   ▼
DefaultAudioSink  ── MvzAudioProcessorChain ──────────────────────────┐
   │                                                                  │
   │   1. TeeAudioProcessor(PcmTap)       ← the PCM tap               │
   │   2. SilenceSkippingAudioProcessor   ← media3's own              │
   │   3. SonicAudioProcessor             ← media3's own (speed/pitch)│
   │   4. NativeDspProcessor              ← EQ, crossfeed, ReplayGain │
   │                                                                  │
   ▼                                                                  │
AudioTrack ──▶ platform audiofx (Equalizer / BassBoost / Loudness) ──▶ speaker
                                       ▲
                        attached to the audio session id,
                        outside the chain entirely
```

The tap (`engine/audio-android/.../PcmTap.kt`) writes into the PCM rings
(`PlaybackSession.captureSink`). `SampleRing`, published as `AudioBus.soundInput`,
feeds each renderer's own `core/sound` engine on its GL thread. Stages 2–4 are
downstream of it.

**When the speaker plays it.** The tap sees a sample when the sink takes it,
a whole AudioTrack buffer (and on Bluetooth a link) before it is heard. The
native playout clock (`core/sound/interp/PlayoutClock`, `geode_playout_*`,
#349) maps the sink's media-time position, which already counts the output
latency, speed and skipped silence, back onto the ring, so a renderer can stop
at the sample the speaker plays. Because the tap is first, the ring holds source
samples: one media second is one source-rate second of ring, at any speed. The
`AudioSink` glue that feeds the clock and the renderer side that reads it are
#488.

## Why the chain is owned rather than configured

`DefaultAudioSink.Builder.setAudioProcessors(...)` looks like it takes an
ordered chain. It does not. It wraps the array in media3's own
`DefaultAudioProcessorChain`, which allocates `length + 2` and **appends**
`SilenceSkippingAudioProcessor` and `SonicAudioProcessor` after everything you
passed in. Both accept `ENCODING_PCM_16BIT` only, so the first stage of ours
that emitted float would make the pipeline's `configure` throw and playback
would die at track start, with a stack trace pointing into media3.

Using `setAudioProcessorChain` and supplying our own puts media3's two stages
*before* ours. That fixes the ordering and the format problem in one move: a
float stage added at position 4 has nothing 16-bit-only left downstream to
offend. With no DSP stages present, `MvzAudioProcessorChain` is byte-for-byte
equivalent to the `setAudioProcessors(arrayOf(tap))` call it replaced.

If you hand-roll `AudioProcessorChain` again, `applyPlaybackParameters` and
`applySkipSilenceEnabled` are not optional plumbing — they are the only route
by which `PlaybackParameters` and the skip-silence toggle reach the two stages
that implement them. A chain that omits them breaks speed, pitch and
skip-silence with no error.

## Two rules this chain keeps

Both were pinned by `AudioChainContractTest` when it existed; the test is not
in the tree, so today they are conventions that this document keeps.

**1. Never enable float output.** `setEnableFloatOutput(true)` reads like the
obvious way to get a float pipeline. It is the opposite. `DefaultAudioSink`'s
own javadoc says *"Audio processing (for example, speed adjustment) will not be
available when float output is in use."* Float output does not give us a float
chain — it removes the chain, and the `TeeAudioProcessor` that feeds every
visual in the app goes with it. The failure is silent: no crash, no log, a
visualizer that never moves and an equalizer that does nothing. **A float chain
is built by having the stages work in float internally**, not by asking the
sink for float output.

This is also why #23 hi-res is a product question rather than an engineering
task: float output is the only escape from media3's int16 conversion, and taking
it deletes the visualizer.

**Bit-perfect USB (API 34+).** ExoPlayer is the only player (the native
AMediaCodec/Oboe player is gone), so bit-perfect is done around its sink, not
inside it: `playback/BitPerfectOutput` hears each stream's format from the
tap's format callback, which Media3 runs before it creates the AudioTrack, and
asks `AudioManager.setPreferredMixerAttributes` for the DAC's
`MIXER_BEHAVIOR_BIT_PERFECT` entry at that rate, 16-bit, same channel mask;
`ExoPlayer.setPreferredAudioDevice` routes the track there. The platform only
treats a track as bit-perfect when its format equals the mixer's exactly, which
is one more reason the chain must stay 16-bit and must not resample.

**Channel counts.** `NativeDspProcessor` runs mono and stereo. For anything
else (5.1, 7.1) its `onConfigure` returns `AudioFormat.NOT_SET`, which makes it
inactive for that track, rather than throwing: a throw in `configure` fails the
sink and ExoPlayer reports the file as unplayable.

**Format changes.** Media3 configures the next item's format while the old
item's tail (a stretch `SilenceSkipping` or Sonic held back) still drains, and
the new format applies only at `flush`. So `NativeDspProcessor` stays a thin
shell and native `core/audio/dsp/DspProcessor` owns the switch: `configure`
stages the next format's chain, the old chain keeps processing the tail, and
`flush` makes the staged one live. Each block goes down with the channel count
it was queued in and its buffer's capacity; a block the live chain cannot fit
passes through untouched. Swapping at configure fed a mono tail to a stereo
chain and wrote past the buffer (#356); `core/tests/dsp_handoff_test.cpp`
pins this.

**2. The tap stays first.** Live and export run the same `core/sound` code, so
they can only agree if they hear the same audio. Live, the tap writes the ring
that each renderer's sound engine reads. Export, `render/offscreen/TrackAudio`
decodes the file itself (MediaExtractor and MediaCodec) and feeds the export
renderer's own engine at 1/fps; that decode has no user DSP in it. Put
user-tunable DSP upstream of the tap and live visuals diverge from every
exported video, differently for every user and every preset.

Stages in the chain after the tap today: `SilenceSkippingAudioProcessor`,
`SonicAudioProcessor`, `NativeDspProcessor`. A new stage goes after them, never
ahead of the tap; add its name to this list when you add it.

## The consequence nobody expects: DSP does not move the visuals

Because the tap is first, **everything the user can do to the sound is
inaudible to the picture.** Fold to mono, cut 12 dB of bass, apply ReplayGain,
crossfeed — the spectrum the scenes draw does not change, because it was
sampled before any of it. The platform equalizer is even further downstream: it
attaches to the audio session id, after `AudioTrack`, outside the chain
entirely.

That is a deliberate trade, not an oversight — it is what buys live/export
parity — but it is surprising enough that it needs saying **once, in the UI, on
every audio-DSP screen**. Otherwise it gets filed as a bug once per feature: the
audio category is a dozen features, and each one will look broken to someone who
turns a knob while watching the visualizer.

If output metering is ever wanted, it gets a **second** tap, downstream, feeding
meters only — never the sound engine.

## Platform effects are a separate mechanism

`AudioFxController` attaches `Equalizer`, `BassBoost` and `LoudnessEnhancer` to
the audio **session id**, not to the chain. Two consequences the UI already
states:

- There is no session until audio starts, so the controls cannot attach before
  playback — hence *"Play something first."*
- Availability is a device lottery; a device may grant one effect and refuse
  another, and the card reports what it actually got rather than pretending.

When an in-chain EQ lands (#24), the two stack silently — both are real, both
apply — so that slice has to decide whether the platform equalizer is retired
or kept behind an explicit "System FX (legacy)" toggle. Shipping both without
deciding means every EQ curve is applied twice on some devices and once on
others.

## Adding a stage

1. Implement `androidx.media3.common.audio.AudioProcessor`, working in float
   internally if it needs the headroom. Do not ask the sink for float output.
2. Pass it in `MvzAudioProcessorChain`'s `dsp` list, which keeps it after the
   tap and after media3's two stages.
3. Add its class name to this document's stage list.
4. Keep filter state across item transitions; reset it on seek. A stage that
   clears its history at every track boundary reintroduces the click gapless
   exists to remove. `onFlush` is the hook, but check on a device *when* media3
   actually calls it before relying on it to tell the two cases apart — this is
   the stage-side half of an invariant the queue side also depends on, and
   only the queue side is testable without a decoder.
5. Configuration belongs on `PlaybackSession`, never on a ViewModel: the service
   and the UI share one session and one player.
6. Add one line of UI copy saying it will not move the visuals.
