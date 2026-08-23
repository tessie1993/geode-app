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
| The two rules that fail the build | `app/src/test/java/dev/musicviz/audio/AudioChainContractTest.kt` |
| Platform effects (a different mechanism) | `audio/AudioFxController.kt`, `ui/EqualizerSettings.kt` |

## The order

```
ExoPlayer
   │
   ▼
DefaultAudioSink  ── MvzAudioProcessorChain ──────────────────────────┐
   │                                                                  │
   │   1. TeeAudioProcessor(PcmTapSink)   ← the analysis tap          │
   │   2. SilenceSkippingAudioProcessor   ← media3's own              │
   │   3. SonicAudioProcessor             ← media3's own (speed/pitch)│
   │   4. …our DSP stages…                ← empty today              │
   │                                                                  │
   ▼                                                                  │
AudioTrack ──▶ platform audiofx (Equalizer / BassBoost / Loudness) ──▶ speaker
                                       ▲
                        attached to the audio session id,
                        outside the chain entirely
```

The tap writes into `PcmRingBuffer`, which `AudioBus` hands to the analysis
engine and every scene. Stages 2–4 are downstream of it.

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

## Two rules that fail the build

`AudioChainContractTest` refuses both of these, so they are not conventions.

**1. Never enable float output.** `setEnableFloatOutput(true)` reads like the
obvious way to get a float pipeline. It is the opposite. `DefaultAudioSink`'s
own javadoc says *"Audio processing (for example, speed adjustment) will not be
available when float output is in use."* Float output does not give us a float
chain — it removes the chain, and the `TeeAudioProcessor` that feeds every
visual in the app goes with it. The failure is silent: no crash, no log, a
visualizer that never moves and an equalizer that does nothing. **A float chain
is built by having the stages work in float internally**, not by asking the
sink for float output.

This is also why #21 bit-perfect and #23 hi-res are a product question rather
than an engineering task: float output is the only escape from media3's int16
conversion, and taking it deletes the visualizer.

**2. The analysis tap stays first.** `ReactiveAnalyzer.reset` and
`AnalysisEngine.reset` both hold live features to matching the cached and
exported features for the same file — the live and offline paths run the same
`ReactiveAnalyzer`, so they can only agree if they are also fed the same audio — and the offline path decodes the file with
no user EQ in it. Put user-tunable DSP upstream of the tap and live visuals
diverge from every exported video, differently for every user and every preset,
with no test able to pin it. The loudness seek bar, drawn from the offline RMS
curve, would disagree too.

The test carries a list of stage names the chain is expected to grow
(`GainProcessor`, `EqProcessor`, `ConvolutionProcessor`, `CrossfeedProcessor`,
`StereoMatrixProcessor`, `DynamicsProcessor`, `DitherProcessor`). Wiring any of
them ahead of the tap fails the build. Add new stage names to that list when you
add the stage.

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
meters only — never `AudioFeatures`.

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
3. Add its class name to `AudioChainContractTest.DSP_STAGE_NAMES`.
4. Keep filter state across item transitions; reset it on seek. A stage that
   clears its history at every track boundary reintroduces the click gapless
   exists to remove. `onFlush` is the hook, but check on a device *when* media3
   actually calls it before relying on it to tell the two cases apart — this is
   the stage-side half of the invariant `GaplessQueueTest` pins on the queue
   side, and only the queue side is testable without a decoder.
5. Configuration belongs on `PlaybackSession`, never on a ViewModel: the service
   and the UI share one session and one player.
6. Add one line of UI copy saying it will not move the visuals.
