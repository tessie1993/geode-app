# Audio feature ABI — time, epoch and channels

The contract every later Phase 2 and 3 slice implements against. Required by
[`MASTER_PLAN.md`](MASTER_PLAN.md) §2.2; the design is §5.1, §5.2 and §5.4.

**Status:** this covers the **time, epoch and channel** sections and the tap's position in the
chain — V2-2-01's scope. The feature table of §5.4 is written by V2-3-03 onward, when the
features exist to describe; a table of names with no producer is a wish list, not an ABI.

---

## 1. Time is samples

Every captured frame carries an absolute input sample index, sample rate, channel count,
source epoch and discontinuity generation. Analysis hops are triggered by new sample
availability, never by a wall-clock timer.

Today's engine does the opposite: the live analyzer runs a **62.5 Hz wall-clock loop** taking
"newest window", while the offline path is 60 Hz sample-locked. Different hop *and* different
alignment, and under load the live one silently drops or repeats audio — the first of the four
divergences [`ENGINE_V2_PLAN.md`](ENGINE_V2_PLAN.md) §2 catalogues.

### 1.1 Reading the ring

`PcmRingBuffer.copyNewSince` returns a count and clamps when a reader has fallen behind, so
"I got fewer samples than I asked for" and "I lost audio" are the same value. §5.1 replaces it
with an explicit result:

```kotlin
sealed interface RingReadResult {
    data class Ok(val firstSample: Long, val sampleCount: Int) : RingReadResult
    data class Gap(val requested: Long, val oldestAvailable: Long) : RingReadResult
    data object NotYetAvailable : RingReadResult
}
```

`Gap` is the one that matters: a reader that fell behind must be told what it missed rather
than handed the oldest audio still present as though it were contiguous.

Requirements, from §5.1: one writer, independently tracked readers; no silent clamp; no
allocation or lock in the audio callback; deterministic epoch reset on seek, source change and
unrecoverable format change; overrun and gap telemetry off the callback thread; stereo
preserved through the analysis boundary; resize and reformat outside the callback.

### 1.2 Epoch

An epoch is a span of continuous sample numbering. It ends on seek, source change, or a format
change the ring cannot absorb. A sample index is meaningful **only** with its epoch — comparing
indices across epochs is the bug the field exists to prevent.

The discontinuity generation increments on every epoch change so a consumer holding a stale
index can tell that it is stale rather than reading plausible nonsense.

In the implementation the tap's generation is a **superset** of §5.1's set, not an exact match:
media3 flushes the processor chain for speed changes, the skip-silence toggle, discontinuity
resync and end of stream as well as for seek and source change. Conservative in the safe
direction — it can report a discontinuity where sample numbering would in fact have continued,
never the reverse.

### 1.3 Channels

Stereo is preserved to the analysis boundary; downmixing happens per-feature, not at capture.
The current offline analyzer downmixes to mono and never constructs `StereoField`, which is why
`stereoWidth` is 0 in every exported video today.

Two existing semantics are preserved deliberately (§1.3): mono input reports
`stereoCorrelation = 1f`, and an empty chroma array means "no pitch information", not twelve
zero-confidence pitch classes.

## 2. Presentation time is not sample time

The tap sits **before** Sonic and silence skipping. Therefore
`presentationTime = sampleTime + offset` is wrong: speed changes scale the mapping and skipped
silence removes spans from it.

§5.2 requires a piecewise `AudioPresentationClock` whose published snapshot is immutable and
whose segments carry:

```text
epoch, inputSampleStart, presentationUsStart, inputSamplesPerPresentationUs,
speed, skippedInputSamples, discontinuityGeneration
```

A segment is appended on seek, speed change, silence-skip discontinuity, route rebuild or
source replacement. The render thread reads an atomic immutable snapshot; the audio callback
never allocates a segment. The mapping runs both directions where possible and surfaces
unmappable gaps rather than interpolating across them.

### 2.1 Which clock "presentation" names

The **output** timeline — the one the listener hears on — not `Player.currentPosition`. The
difference is exactly `speed`: at 2x, one second of presentation consumes two seconds of input,
while media position advances at 2x wall clock. Were presentation media position, `speed` and a
variable slope would both be dead fields in §5.2's own list.

That timeline never runs backwards. A seek does not rewind it — it changes which input frames
land there — so `presentationUsStart` is non-decreasing across every segment while
`inputSampleStart` restarts with each epoch.

Two things the implementation does **not** claim, recorded here rather than discovered later:

- **The absolute offset is unmeasured.** Anchors are relative to the first segment, so audio is
  placed correctly with respect to other audio but carries no term for the sink's own output
  latency. A consumer syncing visuals to what is heard needs that term; it is device work.
- **The timeline counts frames the sink may discard.** A seek reaches the tap through
  `DefaultAudioSink.flush()`, which throws away whatever the output still held. Frames captured
  into the ended generation but never actually heard still advance the anchor, so the clock runs
  slightly ahead across a seek, by the sink's buffered lead.

### 2.2 Silence-skip discontinuity is not an event

§5.2 lists it as an append trigger. **Media3 raises no such event**, and this is a correction
rather than an omission: `SilenceSkippingAudioProcessor` accumulates removed frames inside its
own buffer handling with no callback, and `AudioProcessorChain.applySkipSilenceEnabled` is the
user's *toggle*, not a discontinuity.

So the driver samples the stage's counter at each boundary instead. The consequence is stated
exactly:

| Property | Value |
|---|---|
| Segment **anchors**, skip-silence off | exact |
| Segment **anchors**, skip-silence on | exact to within the stage's *pending* silence buffer at a drain boundary — see below |
| Segment **interiors**, skip-silence on | run late by (silence removed so far within that segment) / slope; corrected at the next boundary |
| Segment interiors, skip-silence off | identically zero — and it is off by default |

The anchor caveat is not a rounding term, so it is named rather than averaged away.
`skippedFrames` is written only inside `outputShortenedSilenceBuffer`, and
`SilenceSkippingAudioProcessor.onQueueEndOfStream` calls it to drain whatever silence the stage
is still holding. On a reconfiguration, media3 cascades `queueEndOfStream` through the pipeline
in ascending order — so the tap, at index 0, reads the counter *before* the stage at index 1 has
added that tail, and the next `onFlush` then zeroes it. The frames are lost to the driver.

Direction and bound: the driver under-counts removed silence, so `heard = captured − skipped` is
too large and the anchor runs **ahead**. The loss per drain boundary is under one
`minimumSilenceDurationUs` of input (100 ms by default) — a longer run would already have been
recognised as silence and output shortened. It does not accumulate within a generation, only
once per drain boundary, and it is identically zero with skip-silence off.

Because the *placement* of removed spans is unknown, every segment carries
`skippedInputSamples = 0` and `PresentationTime.Skipped` is unreachable in production. A §5.2
field and a sealed-interface case ship dead. Locating the spans needs a probe stage between
silence skipping and Sonic, which is its own slice.

The conversion factor being exactly 1 is structural, not luck:
`SilenceSkippingAudioProcessor` counts against its **own** input format,
`TeeAudioProcessor.onConfigure` returns its input format unchanged, and the two stages are
adjacent. `AudioChainOrderRuntimeTest` pins that adjacency, because a resampling stage inserted
between them would corrupt every skip count with no other symptom.

### 2.3 Where the segments come from

Not `Player.Listener`. Those callbacks arrive on the application looper at a time unordered
with respect to the tap's frame counter, so they can say *that* speed changed but not *at which
frame* — and they die with the screen while `PlaybackService` keeps playing.

The driver hangs off the audio sink's own flush points instead, all on the playback thread and
all in one call stack: `MvzAudioProcessorChain.applyPlaybackParameters` /
`applySkipSilenceEnabled` for the parameter, and `PcmTap.flush` for the boundary, which is the
last moment the ended generation's frame count and the skip counter both still exist.

One media3 behaviour is load-bearing and worth naming: `DefaultAudioSink` calls the chain's
`applyPlaybackParameters` **only** when it is not applying playback parameters at the
`AudioTrack`. So a boundary carrying the skip hook but not the speed hook proves Sonic is not
the stage changing speed, and the driver refuses to append rather than model a slope it does
not own.

## 3. Tap stage order

The order, and the reason for each position:

| # | Stage | Why here |
|---|---|---|
| 1 | **visualizer tap** (`TeeAudioProcessor`) | everything below alters what the audio *is* |
| 2 | `SilenceSkippingAudioProcessor` | drops spans; media3's own relative order kept |
| 3 | `SonicAudioProcessor` | resamples for speed and pitch |
| 4 | user DSP | empty today; EQ, gain and dynamics land here |

**The tap is first because analysis must match the offline decode.** `FeatureExtractor.reset`
and `AnalysisEngine.reset` both hold live features to matching what the cached and exported
render produce from the same file, and the offline path decodes with no user EQ in it. A tap
below user-tunable DSP makes live visuals diverge from every exported video — differently for
every user and every preset. The loudness seek bar, drawn from the offline RMS curve, would
disagree too.

Output metering, if it is ever wanted, gets a **second** tap feeding meters only, never
`AudioFeatures`.

`MvzAudioProcessorChain` owns this order rather than inheriting it.
`DefaultAudioSink.Builder.setAudioProcessors` looks ordered but wraps the array and *appends*
media3's two stages after everything passed — and both accept `ENCODING_PCM_16BIT` only, so
the first float-emitting stage would make `configure` throw at track start.

Float **output** is never enabled: `setEnableFloatOutput(true)` disables the sink's whole
processing pipeline, taking the tap with it. A float chain is built by having the stages work
in float internally.

### 3.1 How the order is proved

`AudioChainOrderRuntimeTest` builds the chain the factory installs and asserts against the
array itself: it is non-empty, the first processor is the tap **by identity** (audio pushed
through it arrives at this factory's sink — a second `TeeAudioProcessor` wired elsewhere would
satisfy a type check), media3's two stages come after it, and nothing else in the chain is a
tap.

This exists because the text proof does not survive the move §12 schedules — and, as V2-2-01
found, was already not proving the ordering half: `AudioChainContractTest` guards each stage
comparison with `if (at >= 0)` over seven DSP stage names, **none of which exist yet**, so the
loop body never runs. Both plants below fail the runtime test and pass the text one:

| Planted | Runtime test | Text test |
|---|---|---|
| silence skipping moved before the tap | FAILED — `the tap must be first expected:<0> but was:<1>` | passed |
| tap wired to a different sink | FAILED — `does not feed this factory's sink` | — |

`AudioChainContractTest` is left in place: §2.1 rule 7 forbids removing a legacy seam in the
slice that introduces its replacement.

It survives V2-2-03 as well, and the reason is worth stating rather than leaving as a silent
change of plan. That slice moved the tap's implementation to `PcmTap` in
`:engine:audio-android`, but §12 keeps `TapRenderersFactory` — and the whole Media3 player
workflow — in `:app`. The text test reads the factory, and the factory still says what it said,
so the test still proves the float-output rule it was written for. It retires with the factory,
not with the tap.
