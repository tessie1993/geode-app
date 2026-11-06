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
   │   1. PcmTap                          ← the PCM tap               │
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

The tap (`engine/audio-android/.../PcmTap.kt`) hands each buffer, as bytes, to the
player's native ingest ring (`geode_ingest_write`, `core/audio/ingest`) and reports
each flush's facts on the playback thread, before the next stream's first sample
(#361); native's `FlushJudge` decides whether that is a new epoch (#358, below). No sample is converted in Kotlin. The ring is published as
`AudioBus.playerIngest`; each renderer's native side reads its own cursor on it
every frame (`core/viz/SoundFeed`, `geode_viz_set_sound_source`) into its own
`core/sound` engine, as the stereo downmix, so a 5.1 or 7.1 file's centre and LFE
reach the visuals (#362). Stages 2–4 are downstream of it. The mic and other apps'
audio still write the Kotlin `SampleRing` (`AudioBus.soundInput`) until #485.

**When the speaker plays it.** The tap sees a sample when the sink takes it,
a whole AudioTrack buffer (and on Bluetooth a link) before it is heard. The
native playout clock (`core/sound/interp/PlayoutClock`, `geode_playout_*`,
#349) maps the sink's media-time position, which already counts the output
latency, speed and skipped silence, back onto the ring, so a renderer can stop
at the sample the speaker plays. Because the tap is first, the ring holds source
samples: one media second is one source-rate second of ring, at any speed.

The sink the player builds (`TapRenderersFactory.buildAudioSink`) is
`DefaultAudioSink` wrapped in `app/.../audio/PlayoutSink` (#488), a
`ForwardingAudioSink` that only forwards facts to the clock: each new buffer's
presentation time with the ring position the tap is about to write it at and
its frame count (once per buffer, not per retry), every
`getCurrentPositionUs`, play, pause, flush, reset, and the speed the sink
applied, each stamped with `System.nanoTime`. It sits outside the processor
chain, so the chain's order and the tap's place in it are untouched. The
clock is process-wide (`PlaybackEngine.playout`, never freed), so the app's
stage, the wallpaper and the dream all read the same one; each live
renderer's cursor reads up to the clock's answer and no further
(`core/sound/interp/SpeakerSync`, `core/viz/SoundFeed::pumpUntil`). The mic, another
app's audio and Cast read the newest sample: nothing stands between their
ring and what is heard (Cast's TV plays picture and sound from one stream).
Settings › Audio's A/V offset is applied by the clock, so it reaches every
surface that hears the player.

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
chain — it removes the chain, and the tap that feeds every
visual in the app goes with it. The failure is silent: no crash, no log, a
visualizer that never moves and an equalizer that does nothing. **A float chain
is built by having the stages work in float internally**, not by asking the
sink for float output.

This is also why #23 hi-res is a product question rather than an engineering
task: float output is the only escape from media3's int16 conversion, and taking
it deletes the visualizer.

**Bit-perfect USB (API 34+).** ExoPlayer is the only player (the native
AMediaCodec/Oboe player is gone), so bit-perfect is done around its sink, not
inside it: `audio/TapRenderersFactory` gives the sink an `AudioOutputProvider`
(Media3's default `AudioTrackAudioOutputProvider` in a
`ForwardingAudioOutputProvider`) whose `getAudioOutput` hands each AudioTrack's
config to `playback/BitPerfectOutput` on the playback thread just before the
track is built. That asks `AudioManager.setPreferredMixerAttributes` for the
DAC's `MIXER_BEHAVIOR_BIT_PERFECT` entry with exactly that rate, encoding and
channel mask; `ExoPlayer.setPreferredAudioDevice` routes the track there. (It
used to hear the format from the tap's format callback, which Media3 runs only
after `DefaultAudioSink.handleBuffer` has opened the first track, so a cold
start and every gapless item after it played mixed: #353.) Turning the setting
on or off while an item plays seeks to the current position, which makes
Media3 rebuild the track under the new choice. The platform only treats a track
as bit-perfect when its format equals the mixer's exactly, which is one more
reason the chain must stay 16-bit and must not resample.

While the platform has granted the bit-perfect entry, the playback DSP is pure
pass-through, decided natively: `BitPerfectOutput` only reports the grant
(`geode_dsp_set_bit_perfect`), and `core/audio/dsp/DspProcessor` latches it at
the pipeline flush Media3 runs after every new AudioTrack, then leaves every
block byte for byte as it came. ReplayGain, the fades and crossfade (native gain
ramps, #483), the equalizer and bass boost, loudness, crossfeed and the limiter
all pause; Settings › Playback lists them. The tap is upstream, so the visuals
keep reacting. The int16 ↔ float conversion around the chain is native too
(`geode_dsp_process_pcm16`). `core/tests/bit_perfect_test.cpp` pins the
pass-through; `docs/DEVICE_CHECKS.md` item 43 is the check with a real DAC.

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

**2. The tap stays first** (owner decision, 2026-11-05: considered moving it
after `SilenceSkipping` and Sonic to "hear as played", and kept it first). Live
and export run the same `core/sound` code, so
they can only agree if they hear the same audio. Live, the tap writes the ring
that each renderer's sound engine reads. Export, `render/offscreen/TrackAudio`
decodes the file itself (MediaExtractor and MediaCodec) and feeds the export
renderer's own engine at 1/fps; that decode has no user DSP in it. Put
user-tunable DSP upstream of the tap and live visuals diverge from every
exported video, differently for every user and every preset.

Because it is first, the ring holds **source-time** samples: speed and pitch do
not stretch it and skipped silence is still in it. Following what the speaker
plays at a speed other than 1× is the playout clock's job (#482, #488): it maps
the speaker's media position back to a ring position.

**Flushes and epochs (#358).** Media3 flushes the whole chain for two reasons:
a sink reset (seek, stop, reconfigure; no position) and a re-apply (speed,
pitch or skip-silence change, the first buffer after a reset, a reused-output
item change, a > 200 ms timestamp resync; the chain is drained first and the
flush carries the next buffer's media position and period). `PcmTap` is its own
`BaseAudioProcessor`, not a `TeeAudioProcessor`, because the tee drops that
`StreamMetadata` and also reports end-of-stream as a flush. The chain tells the
tap when it re-applies (`applySkipSilenceEnabled` runs only on that path, just
before the flush), and the tap reports each flush's facts through
`geode_ingest_flush`. Native decides (`core/audio/ingest/FlushJudge`): a reset,
another period or a position jump of more than 1 s begins an epoch, which
restarts every reader's cursor and sound engine; anything else keeps the epoch
with the new format, so the tempo, key and sections the visuals learned survive
a speed slider. `core/tests/ingest_flush_test.cpp` pins it.

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
