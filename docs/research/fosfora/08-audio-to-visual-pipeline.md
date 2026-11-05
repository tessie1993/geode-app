# 08: How fosfora turns audio into pixels, end to end

Fosfora @ `2e85d3b` (fork `tessie1993/fosforatess`, indexed in Graphify at the same commit). Every path is
relative to the checkout root. This report covers fosfora's own design only: no Geode comparison and no
recommendations. Reports 01–07 cover single areas in more depth. This one follows one sample from the
capture callback to a pixel on the swapchain and names the type, thread, clock and buffer at each step.

Method: I read the code and checked call edges in Graphify. Nothing was built or run. Graphify has no edge for
two live calls, and I confirmed both by reading the files. The first is `audio_thread` →
`HopAnalyzer::process_hop` (`crates/fosfora-app/src/audio/mod.rs:1682`); the graph lists only `drive_with` and
tests. The second is `App::update` → `AudioSystem::latest_features` (`crates/fosfora-app/src/app.rs:995`); the
graph reports zero callers. Graphify agrees with the code on the shared per-frame functions:
`mirror_audio_features` and `prepare_effect_layers` each have exactly three production callers, `App::update`,
`SceneRenderer::step` and `LoopSession::render_frame_at`, plus two tests.

Where the docs and the code disagree, the code wins. §8.2 lists each case.

---

## 0. The whole pipeline

```
 OS audio ─┬─ PulseAudio monitor  "fosfora-pulse" thread, f32 stereo, 1024-frame fragments (~23 ms)
           ├─ WASAPI loopback     "fosfora-wasapi" thread, polls every 10 ms
           └─ cpal input stream   OS realtime callback
             │  convert → BS.775 surround fold / mono dup → interleaved L,R      (no heap, no DSP)
             ▼
   RingBuffer  65 536 × AtomicU32 (f32 bits), SPSC, Release/Acquire on write_pos, unpark reader
═════════════╪══════════════════════════════════════ thread boundary ══════════════════════════════
 "fosfora-audio" thread (audio_thread)                    clock: SAMPLE CLOCK  samples_consumed / rate
   park until push → read ≤ 8192 → NaN/Inf := 0 → input meter → mono (L+R)/2 → recording ring
   → trim gain → [decimate ×½ⁿ if > 88.2 kHz] → FIFO (mono + stereo in lockstep)
   while FIFO ≥ 512:  ts = samples_consumed / analysis_rate
       try_lock tempo + structure config (never waits)
       HopAnalyzer::process_hop  (DC → FFT 4096/1024/512 → loudness+gate → stereo → HPSS → YIN →
         contrast → dMFCC → kick → key → pre_norm → normalize → beat → downbeat → structure → smooth)
       → OutputDelay (negative sync offset) → pulse counters (AtomicU32 ×3) → DropOldestSender
═════════════╪══════════════ bounded(4) crossbeam queue, drop OLDEST ═══════ + 3 atomics ═══════════
 main / render thread (winit RedrawRequested)                 clock: Instant, dt ≤ 50 ms
   App::update
     latest_features(dt): drain all → FeatureInterpolator (playhead = newest − 1.5 hops,
        Lerp/Hold per slot, beat & bar phase PLLs) → counter latch beat/downbeat/drop
     mirror_audio_features → ShaderUniforms (448 B template)
     audio textures: waveform (recording ring peek), spectrum (newest), spectrogram (all new mel cols)
     MIDI → OSC → Web drains (last write wins)
     BindingBus::evaluate → clear_modulation → apply_outputs (params, layer fields, uniforms, post)
     trama.update (chain modulation + rate integrals)
     frame_prep::prepare_effect_layers (template → each layer, pack params, rates, particle CPU work)
   App::render
     frame_graph::execute_and_composite
        per layer: write uniform buffer → particle compute → fragment passes (ping-pong) →
                   particle render → layer trama chain → compositor blend
        master trama chain
     PostProcessChain: bloom extract/blur → flash-limit compute → composite → display target
     blit → swapchain (AutoVsync, ≤ 2 frames queued) │ second window │ NDI/Spout/Syphon/v4l2/recording
     layer.flip() (feedback parity advances here only)
═══════════════════════════════════════════════════════════════════════════════════════════════════
 offline: decode → analyze::drive_with → process_hop → SceneRenderer::step (one frame per hop)
          loop export: synth_features(frame) → mirror → prepare → render (no analysis at all)
```

Sources for the diagram: backends `crates/fosfora-app/src/audio/mod.rs:244-260`,
`crates/fosfora-app/src/audio/pulse_capture.rs:156-159`, `crates/fosfora-app/src/audio/wasapi_capture.rs:322-323`;
ring `crates/fosfora-app/src/audio/capture.rs:94-180`; audio thread `crates/fosfora-app/src/audio/mod.rs:1510-1711`;
queue `crates/fosfora-app/src/audio/mod.rs:549-552`; render loop `crates/fosfora-app/src/main.rs:375-376`,
`:4625`, `:4655`; update `crates/fosfora-app/src/app.rs:927-1625`; render `crates/fosfora-app/src/app.rs:4413-4682`.

---

## 1. Capture and input

### 1.1 Backends

`open_backend` tries the native loopback first (PulseAudio on Linux, WASAPI on Windows) and falls back to cpal.
Naming a device skips the native backends (`crates/fosfora-app/src/audio/mod.rs:244-260`). All three
backends deliver interleaved stereo f32 into the same `RingBuffer`:

- **PulseAudio**: `PA_SAMPLE_FLOAT32LE`, 2 channels, fragment size 2048 samples = 1024 stereo frames,
  "≈ 23ms at 44.1kHz", maxlength 4 fragments (`crates/fosfora-app/src/audio/pulse_capture.rs:156-159`, `:215-230`).
  Each blocking `pa_simple_read` result is pushed whole (`:500`).
- **WASAPI**: shared-mode loopback with a 1 s buffer request (`crates/fosfora-app/src/audio/wasapi_capture.rs:55-56`,
  `:277-284`). The thread sleeps 10 ms, then drains every packet (`:322-376`). A packet flagged SILENT is still
  pushed, as zeros, "skipping them stops the analysis sample clock, so the beat tracker sees time jump when
  sound resumes (#69)" (`:352-359`). Surround mix formats fold to L/R by the speaker mask (`:303-314`).
- **cpal**: the OS callback converts I16, I32 or F32 straight into the ring through `push_iter`. Mono is
  duplicated. More than two channels go through `Downmix` "(#67: keeping only channels 0/1 lost the
  centre)". "a heap allocation can block on the allocator's lock and cause a dropout (#50)"
  (`crates/fosfora-app/src/audio/capture.rs:376-418`).

The downmix uses ITU-R BS.775 Lo/Ro: fronts at unity, centre and surrounds at −3 dB, LFE dropped
(`crates/fosfora-app/src/audio/downmix.rs:1-9`).

### 1.2 The SPSC ring

`RingBuffer` has 65 536 slots (a power of two, masked) of `AtomicU32` that hold `f32::to_bits` (#49). Slot
reads and writes are Relaxed. `write_pos` is published with Release after the last sample of a push and read
with Acquire (`crates/fosfora-app/src/audio/capture.rs:94-153`, `:159-180`). If the producer laps the consumer,
`read` skips to the newest 65 536 samples. Because `RING_SIZE` is even, the skip keeps L/R parity
(`:164-170`). At 44.1 kHz the ring holds 32 768 stereo frames, about 0.74 s. Each push unparks the registered
reader (#73, `:124-153`). A second, non-consuming `peek_latest` serves the render-side waveform
(`:182-200`).

### 1.3 The audio thread: from ring to hop

`audio_thread` (`crates/fosfora-app/src/audio/mod.rs:1510`) runs on a thread named `fosfora-audio`
(`:573-592`). It parks for up to 20 ms when the ring is empty (`:209-211`, `:1584-1591`). On each wake it does
the following, in this order:

1. It reads up to 8192 samples (4096 frames) (`:1551`, `:1593-1594`).
2. It replaces every non-finite sample with 0 "before anything reads it (#51)", because one NaN "permanently
   poisons every running sum and EMA downstream" (`:1598-1605`).
3. It meters the raw input before the trim (#84, `:1606-1607`).
4. It mixes mono as `(L+R)/2` (`:1611-1615`) and mirrors the mono to the recording ring. A linear resampler
   runs here if the recording rate differs from the device rate (#79, `:1553-1564`, `:1617-1624`).
5. It applies the input trim to analysis only (`:1625-1633`).
6. Above 88.2 kHz it decimates by ½ⁿ with a 63-tap Blackman half-band filter, so the chain runs at
   44.1–88.2 kHz. The filter's added delay is about 0.3 ms at 96 kHz (`crates/fosfora-app/src/audio/decimate.rs:10-11`,
   `:23`, `:36-38`; `crates/fosfora-app/src/audio/mod.rs:1537-1547`, `:1636-1643`).
7. It appends mono to `fifo` and the interleaved stereo to `fifo_stereo` in lockstep (`:1566-1572`).

### 1.4 The sample clock and hop slicing

`ANALYSIS_HOP = 512` at every rate: 11.6 ms and 86.13 Hz at 44.1 kHz, 93.75 Hz at 48 kHz
(`crates/fosfora-app/src/audio/mod.rs:1462-1478`). Constants that were tuned in hops at 44.1 kHz are carried
to other rates by `rescale_hops` and `rescale_decay` (#53, `:1469-1490`). For every complete hop in the FIFO
(`:1645-1703`):

- `samples_consumed += 512`, then `timestamp = samples_consumed / analysis_rate`. The stamp is therefore the
  **end** of the hop, on the sample clock: "frame timing is exact and independent of scheduler jitter or
  read-burst size" (`:1566-1568`, `:1652-1653`).
- The tempo control and the structure tuning are snapshotted with `try_lock_now`, which returns `None` when
  the lock is held. A busy lock keeps the last snapshot, so a UI panel that holds the lock for a whole draw
  never stalls a hop (#77, `:1499-1508`, `:1655-1676`).
- The positive half of the user's A/V offset becomes the beat detector's sync lead (`:1678-1681`).
  `process_hop` runs (`:1682-1684`).
- The output goes into `OutputDelay`, which releases it once `ts + delay ≤ now`. With a delay of 0 it passes
  straight through (`:1685-1687`; `crates/fosfora-app/src/audio/sync_delay.rs:1-39`). The negative offset
  ranges down to −250 ms and the positive one up to +200 ms (`crates/fosfora-app/src/audio/beat/control.rs:132-135`).
- The three pulses increment their atomic counters, and then the frame is sent. The send never blocks; when
  the queue is full the oldest frame goes (`crates/fosfora-app/src/audio/mod.rs:1688-1702`).

The consumed samples are drained from the FIFO and the sub-hop remainder is kept (`:1705-1709`).

---

## 2. Analysis: `HopAnalyzer::process_hop`

The module header states the contract: every stateful detector runs "in the one order that is correct", and
"Nothing in this module touches wall-clock time or a shared lock", "That is what lets an offline file driver
run the identical chain … and get the same numbers a live session would produce" (#2027,
`crates/fosfora-app/src/audio/hop.rs:1-13`). Smoothers use a fixed `dt = 512 / sample_rate`
(`:135-137`, `:175`).

| # | Stage (hop.rs) | What happens | Output |
|---|---|---|---|
| 1 | DC strip `:211-220` | one-pole `y = x − x₁ + R·y₁`, R = exp(−2π·5/sr), applied to mono, L and R (#55, `:35-68`) | clean hop |
| 2 | Multi-res FFT `:222-224` | the hop shifts into a 4096-sample window, then three Hann FFTs run on its tail: 4096 (10.8 Hz/bin; sub, bass, kick), 1024 (43 Hz; mids), 512 (86 Hz; highs). Magnitude × 2/Σw, so a full-scale sine reads 1.0 (#54) (`crates/fosfora-app/src/audio/analyzer.rs:9-12`, `:61-130`, `:441-461`) | three magnitude spectra plus the previous frame of each |
| 3 | Bands, shape `analyzer.rs:576-696` | default `BandScale::Db`: dB over a −60..0 window, +3 dB/oct tilt above 2 kHz, −6.02 dB calibration offset (`:41-48`, `:587-610`). `rms` comes from the last 2048 samples (`:615-618`). Centroid and bandwidth are the mean and spread of power-weighted log2 f, 40 Hz–18 kHz (`:740-761`); flux is the half-wave log-magnitude rise with floor 2e-3 (`:768-783`); also rolloff, zcr, flatness, 13 MFCCs, CQT chroma and `dominant_chroma = argmax/11` (`:646-692`) | `raw: AudioFeatures` |
| 4 | Loudness + **gate** `:226-234` | BS.1770 K-weighting per channel, L²+R² summed, running sums over 400 ms and 3 s; −60..0 LUFS → 0..1. The gate opens at ≥ −55 LUFS and closes after 250 ms below −60; it starts closed (`crates/fosfora-app/src/audio/loudness.rs:21-54`, `:207`, `:216-250`) | `loudness_*`, `loud_silent` |
| 5 | Stereo `:236-253` | pan, width and correlation plus 7 band pans; the stage gates on its own energy | 10 Passthrough fields |
| 6 | HPSS `:255-265` | median-filter masks on the 1024 spectrum, gated on `loud_silent` | 3 fields |
| 7 | Pitch `:267-276` | YIN on the raw 4096 window, held through unvoiced gaps | `pitch`, `pitch_confidence` |
| 8 | Contrast, dMFCC `:278-294` | per-octave contrast from 200 Hz up (zero under the gate, `analyzer.rs:798-815`); the delta-MFCC slope vector goes to the frame | 8 fields + `dmfcc[13]` |
| 9 | Kick `:296-300` | gradient-boosted trees over 11-band log-flux and level plus low-span flatness and peakiness, for this hop and the two before it, "No lookahead, so no added latency" (`crates/fosfora-app/src/audio/kick_model.rs:9-13`, `:32-46`). The probability is mapped so that the threshold reads 0.5 (`:131-139`), and the gate forces 0 (`analyzer.rs:501-506`) | `kick` |
| 10 | Key `:302-310` | braw profile correlation against a 20 s energy-weighted mean; a challenger needs a +0.05 margin held for 3 s (`crates/fosfora-app/src/audio/key.rs:29-35`, `:90-138`) | `key_class`, `key_is_minor`, `key_confidence` |
| 11 | **pre-norm snapshot** `:323-332` | copies of chroma, 3-band flux and the whole `raw` struct, taken *before* normalisation, for downbeat, structure and offline use | `pre_norm` |
| 12 | Normalise `:334-336` | applies each slot's `NormPolicy` (§2.1) | `raw` in 0..1 |
| 13 | Beat `:338-385` | onset → tempo → scheduler on the raw spectra (§2.2); `bpm` stored as BPM/300 | 7 fields |
| 14 | Downbeat `:387-463` | runs on the beat's *settled* instant (#4473) with pre-norm RMS and chroma; with a sync lead, a downbeat is predicted to fire with its beat (#80) and both phases are advanced by the lead (`lead_phase`) | `downbeat`, `bar_phase`, `beat_in_bar`, indices |
| 15 | Structure `:465-475` | reads `pre_norm` and the settled beat; heavy work runs on a 10 Hz tick (`crates/fosfora-app/src/audio/structure.rs:56`) | `section_novelty`, `buildup`, `drop` |
| 16 | Smooth `:477-478` | asymmetric EMA per slot, α = 1 − exp(−dt/τ) (`crates/fosfora-app/src/audio/smoother.rs:18-43`) | `smoothed` |
| 17 | Frame `:493-525` | `AudioFrame` plus `beat_fired`, `downbeat_fired`, `drop_fired` read *before* smoothing (`:70-77`) | `HopOutput` |

The gate is one flag that every detector reads. The onset detector, HPSS, YIN, contrast, dMFCC, kick, the
normaliser, the beat phase freeze and the downbeat tracker all take `loud_silent`
(`crates/fosfora-app/src/audio/hop.rs:233-234`, `:260-300`, `:336`, `:358`, `:407`).

### 2.1 Normalisation policies

Each slot's policy comes from `schema::FEATURES` (`crates/fosfora-app/src/audio/normalizer.rs:77-127`):

- `Adaptive`: a 344-hop (~4 s) window per slot, `(v − P5)/(P95 − P5)`, with a soft knee above 0.85; a
  span under 1e-6 reads 0. Under the gate the window is **frozen** (nothing is pushed) and the output is 0
  (`:9-26`, `:87-96`, `:130-147`).
- `FixedRange`: clamp to 0..1 and hold the last value under the gate (`:98-106`).
- `ZScore` (the MFCCs): EMA mean and variance with α = 0.01, mapped through `0.5 + 0.5·tanh(z/3)`; it reads
  0.5 under the gate (`:108-121`).
- `Passthrough`: the producer owns the range (`:85`).

### 2.2 Beat, tempo and grid

`BeatDetector::process` runs in three stages (`crates/fosfora-app/src/audio/beat/mod.rs:271-379`):

1. **Onsets.** Onsets are detected on the raw spectra. A beat-activation tree scores every hop; its peaks
   are placed `act_lag` hops back (`:286-306`). A 50 ms cooldown applies (`:163`, `:308-315`).
2. **Tempo.** The tempo estimator recomputes its autocorrelation every 6 hops (`crates/fosfora-app/src/audio/beat/tempo.rs:471-477`).
3. **Scheduling.** The `BeatScheduler` runs a phase-locked grid on the sample clock. A locked grid **emits
   each beat `BEAT_LEAD_SECS` = 10 ms before its instant** because "its instants sit late" by ~15–19 ms
   against annotations. Only the emission moves; "the instant itself settles where it did"
   (`crates/fosfora-app/src/audio/beat/scheduler.rs:49-61`). A user lead adds to that, capped at 0.45 of a
   period (`crates/fosfora-app/src/audio/beat/control.rs:136-141`; `scheduler.rs:535-539`).

`beat_phase` is pinned to 0 under the perceptual gate, not on `rms`, which "floors it at exactly 0.0 … the
trough between every hit" (`beat/mod.rs:350-358`). `bpm` is published held: 0 until the first lock, then the
last locked tempo (`crates/fosfora-app/src/audio/features.rs:142-150`).

### 2.3 The `AudioFeatures` ABI and the schema

`#[repr(C)] AudioFeatures` is 85 × f32 = 340 B (`crates/fosfora-app/src/audio/features.rs:11-124`, test
`size_is_340_bytes` `:227-231`). Fields are only ever appended, in batched bumps (v2 #1505, v3 #1629,
band-pan #1801, v4 overlay clock, tempo trust #81), "so every existing feature index stays put"
(`:97-121`, test `:197-225`). Values are 0..1, with three exceptions:

- `bar_index` and `beat_index` are raw counts (`:108-116`).
- `bpm` is BPM/300. Time maths must use `raw_bpm()` or `beat_period_secs()`, because `60/bpm` on the
  normalised value "made timeline transitions 300× too slow (#2054)" (`:126-150`).
- `beat_clock() = beat_index + beat_phase` is the monotonic musical clock (`:152-157`).

`schema::FEATURES: [FeatureDef; 85]` is "the one ordered table" that the normaliser, smoother, interpolator
and stall decay read positionally (`crates/fosfora-app/src/audio/schema.rs:1-16`, `:118`). Each row holds a
name, `NormPolicy`, `SmoothParams{attack, release, bypass}`, `DecayPolicy{Scale, Hold, ForceZero}` and
`InterpPolicy{Lerp, Hold}` (`:20-111`). A const assert pins the length (`:509`). The 12 Hold rows are the
triggers, the wrapping phases, the categorical indices, the counters and `beat_locked`; 73 rows Lerp
(`:106-110`). Two rules matter downstream:

- Circular values bypass the EMA. `dominant_chroma` once "rainbow-swept BACKWARDS through the whole colour
  wheel" on a B→C change (`:196-213`).
- Position-like bipolar fields smooth symmetrically, because asymmetric pairs biased `pan` to 0.659 on an
  alternating 0.2/0.8 source (#1920, `:272-279`).

### 2.4 `AudioFrame` and its texture streams

`AudioFrame` (`crates/fosfora-app/src/audio/mod.rs:54-109`) carries, in one message:

- `features`;
- `spectrum: [f32; 512]`, a log-frequency peak-per-bin spectrum from 30 Hz to Nyquist off the 4096 FFT,
  mapped −80..0 dB → 0..1 (`crates/fosfora-app/src/audio/analyzer.rs:519-550`, `:199-205`);
- `mel: [f32; 64]`, one column of a dedicated 64-band mel bank (`:552-568`);
- `dmfcc: [f32; 13]`, used by bindings only;
- `timestamp`;
- `phase_frozen`, the gate flag itself, carried explicitly because the smoothed rms "lags the freeze by
  ~1s" (`mod.rs:75-81`);
- `bar_duration`, refreshed only on a downbeat (`:82-89`);
- `beat_time` and `downbeat_time`;
- `section_boundary`, a trigger kept out of the uniform ABI (`:99-108`).

The arrays are inline, "so producing a frame allocates nothing (#75)" (`:61-63`).

---

## 3. Hand-off to the render thread

**Queue.** `inbound::bounded(4)` wraps a crossbeam channel. Its sender holds a second receiver and evicts the
oldest message when the queue is full, "so the newest always arrives" after a render stall (#59, #103;
`crates/fosfora-app/src/inbound.rs:1-59`; capacity `crates/fosfora-app/src/audio/mod.rs:549-552`).

**Pulse counters.** `beat`, `downbeat` and `drop` are one-hop triggers that a drain-to-newest consumer would
lose. The audio thread therefore also increments three `AtomicU32` counters (Relaxed), and the render side
derives each pulse from "any advance since the last poll" (`mod.rs:111-125`, `:1112-1142`, `:1688-1699`).
`pulse_counts()` exposes the totals to rate-limited consumers such as OSC without touching the latch
(`:1162-1174`).

**`latest_features(dt)`** (`mod.rs:1049-1145`), called once per render frame:

1. Drain the queue. Every frame is pushed into the interpolator, the newest spectrum, mel column and dmfcc
   are kept, and *every* mel column is queued for the spectrogram texture (`:1054-1075`).
2. **Decay on stall.** If no frame has arrived for 250 ms, the held vector decays by k = exp(−poll_dt/0.3)
   per poll, following each slot's `DecayPolicy` (Scale: ×k; Hold: `bpm`, `key_class`, `pitch`, counters;
   ForceZero: triggers and `beat_locked`). The interpolator is reset, otherwise "leaving its ring live here
   would keep serving the last *undecayed* frame" (`:1077-1093`, `:1431-1444`).
3. `interp.sample(dt, latest)` (`:1110`).
4. The counter latch overrides `beat`, `downbeat` and `drop`. It "Runs last so it always wins over the
   interpolated slots" (`:1112-1144`).

**`FeatureInterpolator`** (`crates/fosfora-app/src/audio/interp.rs`):

- **Ring and playhead.** The ring holds the newest 4 timestamped frames (`:34-39`). The playhead runs on the
  *sample clock* `TARGET_DELAY_HOPS = 1.5` hops behind the newest frame (≈ 17.4 ms at 44.1 kHz). The extra
  hop of slack absorbs bursty delivery, and the doc says this "does not delay beat *timing* at all"
  (`:22-32`). Each render frame the playhead free-runs by `dt`, then trims its error toward the target with
  τ = 0.25 s. The correction is capped at 10 % of `dt`, so the playhead "never stalls or reverses", and it
  snaps past 0.25 s of error (`:41-51`, `:195-213`).
- **Blend.** Lerp slots blend the two frames that bracket the playhead with α clamped to 0..1, never
  extrapolating. Hold slots come from the **older** frame "so the whole vector stays temporally consistent"
  (`:224-252`, `:373-387`). `phase_frozen` and `bar_duration` also come from the older frame (`:218-223`).
  A non-monotonic timestamp (a device switch restarts the clock) clears the ring (`:127-133`).
- **Phase PLLs.** `beat_phase` and `bar_phase` are replaced every render frame by local first-order PLLs. The
  beat runs at `bpm/60` and the bar at `1/bar_duration` (`:260-321`). Each advances by `dt`, then pulls
  toward the detector's phase along the wrap-aware shortest path with τ = 0.15 s, a backstop of 10 % of a
  cycle per frame, and a snap threshold of `min(τ·rate, 0.25)`. That threshold is derived so that the phase
  "only ever rises or wraps" (`:53-69`, `:333-371`). Both PLLs follow the detector while `phase_frozen` is
  set or below 20 BPM (`:268-273`, `:303-314`).
- **Index repair.** `reconcile_index` re-pairs the held `bar_index` and `beat_index` with the locally
  advanced phases, so `index + phase` never pops by a whole slot at a wrap (`:180-189`, `:389-409`).

**Sync delay.** The negative half of the offset is applied on the audio thread before the queue, to "levels,
bands and phases as much as the pulses" (`crates/fosfora-app/src/audio/sync_delay.rs:1-7`). The positive half
is a prediction inside the beat scheduler (§2.2).

---

## 4. Per frame on the render thread

`main.rs` calls `app.update()` on `RedrawRequested`, then `app.render()`, then `request_redraw()`
(`crates/fosfora-app/src/main.rs:375-376`, `:4625`, `:4655`). The swapchain uses `AutoVsync` with
`desired_maximum_frame_latency: 2` (`crates/fosfora-app/src/gpu/context.rs:159-167`).

`App::update` (`crates/fosfora-app/src/app.rs:927`) runs in this order:

1. `dt = min(now − last, 0.05)`. A hitch "integrates as one giant step"; "Momentary slow-motion during a stall
   beats a white flash" (#1796, `:939-947`).
2. `u.time = shader_time(secs since start)`, which wraps every 3600 s so an f32 `sin(u.time·k)` never
   stutters (`crates/fosfora-app/src/gpu/uniforms.rs:240-253`). Also `delta_time`, `resolution`,
   `feedback_decay = 0.88` and `frame_index` (`app.rs:984-992`).
3. `latest_features(dt)` → `self.latest_audio` → `mirror_audio_features` copies every field into the
   `ShaderUniforms` template (`app.rs:994-998`). The mirror is "Extracted verbatim … so the headless scene
   renderer (#2027) forwards features identically — a slot missed here is a shader input that silently stays
   0.0 offline while moving live" (`uniforms.rs:255-330`).
4. **Audio textures** (`app.rs:1021-1051`; `crates/fosfora-app/src/gpu/audio_textures.rs`):
   - waveform: `Rg16Float` 1024×1 holding min/max of 2048 samples peeked from the *recording ring*, aligned
     to a rising zero crossing in the first 512 samples (`audio_textures.rs:21-28`, `:142-170`);
   - spectrum: `R16Float` 512×1 holding the newest frame's spectrum (`:173-185`);
   - spectrogram: `R16Float` 512×64. Each new column is low-passed per band (α = 0.4), the rows shift left,
     and the whole texture is re-uploaded in linear time order, because "The ABI is frozen (no
     `spectrogram_head` uniform)" (`:8-11`, `:30-62`, `:190-220`).

   `u.scroll_phase` extrapolates between column commits from an EMA of the commit interval, so the terrain
   scrolls smoothly (#1508, `app.rs:1035-1051`).
5. The device watchdog runs (`app.rs:1053-1059`). MIDI, OSC and WebSocket drain into the active layer's
   `ParamStore` base values, last write wins (`:1061-1317`).
6. **Binding bus** (`app.rs:1319-1348`):
   - `evaluate` builds a `SourceSnapshot` of the interpolated features under `audio.*` ids, LFOs on
     `beat_clock()`, mel bands, dmfcc, MIDI, OSC and web values
     (`crates/fosfora-app/src/bindings/bus.rs:368-394`; ids `crates/fosfora-app/src/bindings/sources.rs:73-128`,
     which also derives `audio.key_hue`).
   - Each enabled binding skips a non-finite source (#46), runs its transform chain (Remap, Smooth, Invert,
     Quantize, Deadzone, Curve, Gate, Scale, Offset, Clamp) and records a rising edge when it crosses 0.5
     (#1791) (`bus.rs:464-504`; `crates/fosfora-app/src/bindings/transforms.rs:5-74`).
   - `layer_stack.clear_modulation()` then `apply_outputs` (`app.rs:1333-1342`). Param targets are scaled
     into the param's range and written with `set_modulated`, an overlay that "a binding never writes" into
     the base: `pack_to_buffer` packs *effective* values, while presets, sliders and saves see the base
     (#4053, #4055; `crates/fosfora-app/src/params/store.rs:5-22`, `:93-133`, `:168-186`;
     `crates/fosfora-app/src/bindings/apply.rs:236-267`).
   - Layer fields, particle fields and master post overlays are written the same way. `Uniform` targets
     overwrite the template slot directly, clamped to 0..1 except `time` (`apply.rs:62-117`, `:162-232`).
7. **trama** `update` advances chain modulation exactly once per frame, "the ONLY place modulation state
   moves" (`crates/fosfora-app/src/trama/mod.rs:136-176`). Its `AudioView` passes features through
   untouched ("a second smoothing stage here would double-smooth") and smooths 32 mel-pair bands itself
   (`crates/fosfora-app/src/trama/audio.rs:1-77`). `resolve_node` combines source × amount with the base (Add,
   Multiply or Replace), clamps, and slews (`crates/fosfora-app/src/trama/modulation.rs:216-264`). The call
   reads `latest_audio` because `latest_features` "consumes a single-consumer pulse latch and must not be
   called again" (`app.rs:1590-1610`).
8. **`frame_prep::prepare_effect_layers`** (`app.rs:1612-1625`; `crates/fosfora-app/src/gpu/frame_prep.rs:24-130`).
   For each effect layer it:
   - copies the global template into the layer;
   - sets `params = param_store.pack_to_buffer()`;
   - calls `rates.advance(params, dt)`. Each `"rates"` param's f64 integral, `value·dt` or `dt/value` for a
     period, is written into the slots *after* the packed params, because `u.time * speed` "jumps the
     picture by (change x uptime)" (#2984; `crates/fosfora-app/src/effect/rates.rs:1-36`, `:158-176`);
   - for particles: `update_uniforms(dt, time, resolution, global.beat)`, forwards all 16 params, sets the
     composite gain, obstacle overlays, `update_audio(&audio)`, and the splat, helix, symbiosis and morph
     drivers (`frame_prep.rs:44-127`).

   The module says why it exists: "Skipping any piece of it produces frames that render without error and
   are silently wrong" (`frame_prep.rs:1-9`).

---

## 5. Rendering

### 5.1 How audio reaches WGSL

`EffectLoader::prepend_library` puts `UNIFORM_BLOCK` and the libraries in front of each effect. It skips the
block only when the shader *declares* `struct PhosphorUniforms` in code; comments are stripped first, after a
comment mentioning the name suppressed injection (#1855) (`crates/fosfora-app/src/effect/loader.rs:356-370`,
`:573-582`). The libraries are noise, palette, sdf, tonemap, chronoflow and overlay_lib (`:95-102`).

The block (`loader.rs:166-249`) is laid out to match the 448-byte `ShaderUniforms` exactly
(`uniforms.rs:10-136`, test `shader_uniforms_size_448` `:336-344`). Its contents:

- time, dt, resolution, the 7 bands and rms;
- 12 features;
- `params: array<vec4f,4>`;
- `feedback_decay` and `frame_index`;
- `dominant_chroma` and `scroll_phase`;
- `mfcc` and `chroma` as vec4 arrays;
- the v2 and v3 tails, `band_pan: array<vec4f,2>`, the overlay clock and tempo trust.

Bindings: 0 is the uniform buffer, 1 and 2 are `prev_frame` and its sampler, 3–5 are the waveform, spectrum
and spectrogram textures, 6 is the audio sampler (`loader.rs:251-258`). Pass inputs follow at `7+2i` and
`8+2i` (`:296-313`). The helpers are `param(i)`, `mfcc(i)`, `chroma_val(i)`, `band_pan(i)`,
`feedback(uv)` (a sample of `prev_frame`), `waveform(x)`, `spectrum(x)` and `spectrogram(uv)`
(`:260-293`). The fragment entry point is `fs_main` (`crates/fosfora-app/src/gpu/pipeline.rs:219`).

`lib/chronoflow.wgsl` makes feedback frame-rate independent. With `n = clamp(dt·60, ε, 2)` it uses
`k' = kⁿ` and `a' = a(1−k')/(1−k)`, so trail length is no longer "measured in FRAMES" (#1986,
`assets/shaders/lib/chronoflow.wgsl:35-75`).

### 5.2 `PassExecutor`

`execute_profiled` (`crates/fosfora-app/src/gpu/pass_executor.rs:357-440`) runs:

1. `queue.write_buffer` of the layer's uniforms, once.
2. The particle compute dispatch.
3. Each fragment pass as a fullscreen triangle with `LoadOp::Clear`. An iterated (Jacobi) feedback pass
   alternates targets in the encoder so that its last draw lands in `targets[flip_parity]` (`:386-400`).
4. The particle render on top, with Load.

Bind groups exist for both parities. For parity `g`, a feedback pass's own previous frame is
`targets[1−g]`, a current-frame input reads `targets[g]` (or `targets[0]` if the source has no feedback),
and a prev-frame input reads `targets[1−g]` (#1481, `:645-705`). Parity is global per layer (`:88-91`) and
advances only in `flip()` (`:467-479`), which `App::render` calls on every layer after all sinks have captured
(`app.rs:4664-4672`).

### 5.3 Particles' audio coupling

- `update_audio` copies about 30 feature fields into `ParticleUniforms` for the compute and render shaders
  (`crates/fosfora-app/src/gpu/particle/system.rs:1909-1956`).
- `update_uniforms` turns the latched pulse into an emission burst, `if beat > 0.5 && burst > 0 {
  emit_accumulator += burst }`, which fires once per beat at any frame rate because the latch is one frame
  wide (`:1735-1747`).
- Built-in sims read features directly. One example is `brightness = 0.8 + u.rms * 0.5`
  (`assets/shaders/builtin/particle_sim.wgsl:75`).

### 5.4 Layers, compositor, trama, master chain

`frame_graph::execute_and_composite` handles three cases (`crates/fosfora-app/src/gpu/frame_graph.rs:97-260`):

- **No layers:** it returns the cleared accumulator.
- **One fully opaque layer:** it takes a fast path with no compositing (`:166-201`).
- **Otherwise:** it walks the layers bottom first. A layer that samples `@backdrop` first receives a snapshot
  of the composite beneath it (`:203-222`). Each layer runs `execute`, then its own trama chain "in place"
  (`run_layer_chain`), and is pushed with its *effective* (binding-modulated) blend, opacity and displace
  (`:223-248`), then composited (`:252`).

The master chain then post-processes the composite, upstream of tonemapping (`:106-112`, `:267-310`). trama
nodes get the frame's template uniforms with their own params, the resolved modulation and the rate phases
overlaid (`crates/fosfora-app/src/trama/exec/executor.rs:526-540`).

### 5.5 Post-processing, flash limiter, present, sinks

`PostProcessChain::render` receives `u.time`, `u.rms`, `u.onset` and `u.flatness` from the template
(`app.rs:4566-4578`). Audio drives four post effects:

- The bloom threshold drops by `rms·0.15` (`assets/shaders/builtin/bloom_extract.wgsl:21`).
- The bloom mix scales by `0.7 + rms·0.6` (`assets/shaders/builtin/post_composite.wgsl:67`).
- Chromatic aberration is `onset · ca_intensity · 0.03`.
- Grain is `flatness · grain_intensity · 0.08` (`crates/fosfora-app/src/gpu/postprocess.rs:365-395`).

`limit_and_composite` dispatches the flash-limit compute pass, copies its gain into a uniform, and composites
(`postprocess.rs:540-579`). The limiter splits a 64×64 sample of the post colour into 4×4 tiles with a luma
track and a red track each. It counts rises (WCAG / BT.1702: a 10 % luminance step with the dark state below
0.8, or the saturated-red measure). Once a track has spent its budget for the second, the limiter dims the
*whole frame* just under the step (`assets/shaders/builtin/flash_limit.wgsl:1-27`, `:109-148`). It survives
the hourly `u.time` wrap by shifting stored rises (`:118-127`). The budget comes from settings and reduced
motion every frame (`app.rs:4419-4422`).

The display target is blitted to the swapchain and to the second window. NDI, v4l2, Spout, Syphon and
recording all capture from the same `source` through the same post chain, so "every capture sink … get the
same frame" (`app.rs:4557-4662`; `flash_limit.wgsl:3-6`).

---

## 6. Worked examples

### 6.1 A kick becoming a ring in **Pulse**

`assets/effects/pulse.pfx` declares one feedback pass, `pulse.wgsl` (`:7-13`), and post with bloom 0.6 and
threshold 0.5 (`:37-42`).

1. **t₀, the kick hits.** Its samples reach the ring in the next capture block: a Pulse fragment of 1024
   frames, a 10 ms WASAPI poll, or a cpal callback (§1.1).
2. **The hop completes.** The audio thread wakes on the push and appends to the FIFO. The first hop that
   ends after t₀ completes within one hop (≤ 11.6 ms) and is stamped at its end
   (`crates/fosfora-app/src/audio/mod.rs:1645-1653`).
3. **Spectra.** `analyze` shifts the hop into the 4096 window and recomputes all three FFTs on its tail
   (`crates/fosfora-app/src/audio/analyzer.rs:441-461`). The newest samples sit under the Hann window's
   falling edge (`:83-89`, `:106-121`). Inference from that code: the onset's energy grows into the 1024- and
   4096-point spectra over the following hops.
4. **Kick model.** `KickDetector::process` builds the 24-value frame (log-flux of large and medium against
   their previous frames, levels, low-span shape), adds the two previous frames, and scores the ensemble
   (`analyzer.rs:620-629`; `kick_model.rs:190-204`). `kick_envelope` returns 0 under the gate, else
   `kick_from_probability(p)`: ≥ 0.5 at or above the trained threshold (`analyzer.rs:501-506`;
   `kick_model.rs:131-139`).
5. **Into the frame.** `raw.kick` is set before the snapshot. `kick` is Passthrough, so normalisation leaves
   it alone (`crates/fosfora-app/src/audio/schema.rs:132`; `hop.rs:296-300`, `:332-336`). The smoother uses
   attack 2 ms and release 60 ms. At dt = 11.6 ms the attack α is 1 − e^(−5.8) ≈ 0.997, so the rise passes
   almost whole; the fall halves in about 42 ms (`schema.rs:132`; `smoother.rs:33-38`).
6. **Hand-off.** The frame goes through `OutputDelay` (a pass-through at offset ≤ 0) and the queue
   (`mod.rs:1685-1702`). On the next `App::update`, `latest_features` drains it into the interpolator.
   `kick` is a Lerp slot, so the value on screen is the blend at the playhead, **1.5 hops (~17 ms) behind the
   newest frame** (`interp.rs:22-32`, `:224-252`).
7. **Uniform.** `mirror_audio_features` sets `u.kick` (`uniforms.rs:269`). An `audio.kick` binding, if
   present, sees the same interpolated value (`bus.rs:379-383`; `sources.rs:91`). `prepare_effect_layers`
   copies the template into the layer, and `execute_profiled` writes the layer's buffer
   (`frame_prep.rs:38-39`; `pass_executor.rs:365`).
8. **Shader.** `kick_ring_r = u.kick * 0.6`, and the ring adds `exp(−(r − kick_ring_r)²/0.001) · u.kick ·
   1.5` to `ring_sum` (`assets/shaders/pulse.wgsl:46-49`). As the envelope rises the ring jumps out to radius
   ~0.3–0.6; as it releases the ring contracts back toward the centre and dims. `ring_sum` is capped at 2 and
   coloured by `fosfora_audio_palette(r·2 + t·0.08 + phase·0.5, u.centroid, 0)`
   (`pulse.wgsl:56-61`; `assets/shaders/lib/palette.wgsl:9-28`).
9. **Feedback.** `result = max(col, prev.rgb · frame_decay(0.82))` (`pulse.wgsl:21-23`, `:67-70`). The ring
   leaves a trail that keeps 82 % per 1/60 s at any frame rate (`chronoflow.wgsl:58-75`).
10. **Post.** Bloom picks the bright ring up, with the threshold lowered by `rms`
    (`bloom_extract.wgsl:21`). The flash limiter may dim the frame if a tile exceeds its rise budget
    (`flash_limit.wgsl:129-148`). Then the composite, the blit and the vsync present.

The same kick usually also produces the **beat pulse** a few milliseconds earlier. The scheduler emits the
grid beat 10 ms before its instant (`scheduler.rs:49-61`). The counter latch makes `u.beat = 1.0` for exactly
one render frame with no playhead delay (`mod.rs:1117-1125`). Pulse draws it as a centre flash,
`exp(−r²·80)·u.beat·2` (`pulse.wgsl:63-65`). The beat-phase rings follow the local PLL phase, not the raw hop
phase (`pulse.wgsl:18`, `:30-44`; `interp.rs:176-177`).

### 6.2 A key change becoming a palette rotation in **Chromatica**

1. **Key chroma.** Every hop, the CQT-lite chroma on the 4096 spectrum yields an unnormalised pure-fold
   energy vector `e12` for the key detector (`analyzer.rs:675-681`).
2. **Detector.** `KeyDetector::process(e12, dt)` folds it into an energy-weighted mean with τ = 20 s. A
   challenger key must beat the incumbent's correlation by 0.05 continuously for 3 s before `key_class`
   changes (`key.rs:29-35`, `:90-138`; call `hop.rs:302-310`).
3. **Policies.** `key_class` and `key_is_minor` are Passthrough, bypass smoothing, Hold on stall and Hold in
   the interpolator: "no EMA blend across key changes … no sweep toward C" (`schema.rs:241-247`). The new
   key therefore arrives as a step on one hop. It reaches the screen at the playhead after the older
   bracketing frame has moved past it, roughly 1–2 hops later.
4. **Shader.** `fosfora_key_hue` rounds `key_class·11` to a pitch class, maps it onto the circle of fifths
   (`pc·7 mod 12`) and shifts it by −0.08 for minor (`assets/shaders/lib/palette.wgsl:61-65`). Chromatica adds
   `key_hue` to every ring hue, the centre bloom and the node colours. In a minor key, `minor_droop` sags and
   squashes the geometry and the ring colours mix 25 % toward `.bgr`
   (`assets/shaders/chromatica.wgsl:49-54`, `:90-92`, `:104`, `:146`, `:157`). A key change rotates the whole
   palette by a fifth-step multiple in one frame.
5. **Softening.** The feedback trail blends the change over a few frames
   (`col·frame_gain(1, fb_keep) + prev·frame_decay(fb_keep)` with `fb_keep = feedback_amount·0.5`,
   `chromatica.wgsl:150-154`). `key_confidence`, which is smoothed (`schema.rs:248-253`), scales the
   consonance edges (`chromatica.wgsl:137-138`).
6. **Rates.** The mandala's own motion is rate-integrated. `"rates": ["rotation_speed", "edge_spin"]`
   (`assets/effects/chromatica.pfx:100`) puts their integrals in `param(12)` and `param(13)`, so moving a
   speed changes speed, not position. The shader recovers `∫(2·p10 − 1)dt` as `param(13)·2 − t`, the remap
   trick that `rates.rs:18-20` describes (`chromatica.wgsl:58-61`, `:70`).

A binding can do the same for any effect: `audio.key_hue = 0.5 + (hue − 0.5)·key_confidence`, which pulls the
hue toward a neutral 0.5 while confidence is low (`bindings/sources.rs:60-61`, `:121-128`).

---

## 7. Timing, latency and the offline paths

### 7.1 Every clock and rate

| Clock / rate | Value | Defined at | Drives |
|---|---|---|---|
| Device sample rate | as opened (Pulse requests 44.1 kHz) | `pulse_capture.rs:395`; `capture.rs:287-288` | capture, recording ring |
| Analysis rate | device rate halved while > 88.2 kHz | `decimate.rs:25-38` | every analysis stage |
| Capture block | Pulse 1024 frames (~23 ms); WASAPI poll 10 ms | `pulse_capture.rs:156-159`; `wasapi_capture.rs:322-323` | when the ring fills |
| **Hop / sample clock** | 512 samples: 11.6 ms, 86.13 Hz @ 44.1 kHz; 93.75 Hz @ 48 kHz; `ts = samples_consumed/rate` | `mod.rs:1462-1478`, `:1652-1653` | `AudioFrame.timestamp`, all detectors |
| Smoother dt | fixed 512/sr | `hop.rs:175` | per-slot EMAs |
| Tempo ACF | every 6 hops (~14 Hz) | `beat/tempo.rs:471-477` | `bpm`, period |
| Structure tick | 10 Hz | `structure.rs:56` | novelty, buildup, drop |
| Render frame | `Instant` dt, clamped ≤ 50 ms; vsync | `app.rs:939-947`; `context.rs:159` | playhead, PLLs, rates, sims |
| `u.time` | wall seconds since start, wrapped at 3600 s | `uniforms.rs:240-253`; `app.rs:985-986` | shaders, flash window |
| Interp playhead | sample-clock seconds advanced by render dt, 1.5 hops behind, τ 0.25 s trim | `interp.rs:22-51`, `:195-213` | Lerp/Hold sampling |
| Local beat/bar phase | `bpm/60` and `1/bar_duration`, PLL τ 0.15 s | `interp.rs:53-72`, `:333-371` | `beat_phase`, `bar_phase` |
| Pulse counters | monotonic per event | `mod.rs:1688-1699` | `beat`, `downbeat`, `drop` |
| Spectrogram history | 512 columns ≈ 5.9 s at 86 Hz | `audio_textures.rs:30-31` | `spectrogram(uv)` |
| Scroll phase | EMA of commit interval | `app.rs:1035-1051` | `u.scroll_phase` |
| Rate integrals | f64, `value·dt` per render frame | `rates.rs:158-176` | `param(N)` integrals |
| Flash window | 1 s on `u.time` | `flash_limit.wgsl:27`, `:68-71` | frame gain |
| OSC TX / web snapshot | 30 Hz / 10 Hz | `ARCHITECTURE-NOTES.md:99-121` | external consumers |
| Headless tick | one frame per hop | `headless/driver.rs:6-9`, `:149` | offline renders |
| Loop export | `frame/fps` arithmetic | `headless/loop_driver.rs:42-77`, `:356-370` | synthetic features |

### 7.2 Latency budget for a continuous feature (44.1 kHz)

The terms come from the code. The sums are my arithmetic.

| Stage | Cost | Source |
|---|---|---|
| Capture block | Pulse ≤ ~23 ms, WASAPI ≤ ~10 ms, cpal = device buffer | §1.1 |
| Waiting for the hop to complete | 0–11.6 ms (mean ~5.8) | `mod.rs:1645-1653` |
| Analysis window | the 4096 window spans 93 ms, its centre 46 ms before the hop stamp; rms over the last 2048 (centre 23 ms back); the kick uses 1024- and 4096-point flux with no lookahead | `analyzer.rs:10-12`, `:615-618`; `kick_model.rs:13` |
| Smoothing | attack τ 2–80 ms per slot (kick 2 ms, bands 5–20 ms) | `schema.rs:120-142` |
| Queue and poll | 0 to one render frame (6.9 ms @ 144 Hz, 16.7 ms @ 60 Hz) | `mod.rs:1049-1075` |
| Interpolation playhead | +1.5 hops = 17.4 ms | `interp.rs:22-32` |
| GPU and present | up to 2 queued frames (`desired_maximum_frame_latency: 2`) | `context.rs:167` |
| User offset | −250 ms (hold everything) to +200 ms (lead the beat) | `beat/control.rs:132-135` |

Triggers and phases skip the playhead. The beat pulse comes from the counter latch, and the phase comes from
the local PLL (`interp.rs:29-31`). A locked beat is also emitted 10 ms before its grid instant, to offset the
measured ~15–19 ms lateness of the grid (`scheduler.rs:49-61`). Slow features add their own lags: a key
switch takes ≥ 3 s of sustained evidence (`key.rs:33-35`), and a section boundary is reported
`BOUNDARY_LAG_SECONDS = KERNEL_SECONDS + CONFIRM_SECONDS = 6 s` late (`structure.rs:61`, `:92`, `:381`;
`mod.rs:104-107`).

### 7.3 Offline paths: what is shared and what differs

| | Live (`audio_thread` + `App`) | `analyze::drive_with` (`--analyze`, `--signal-dump`) | `SceneRenderer::step` (`--render-scene`) | Loop export (`--render-loop`) |
|---|---|---|---|---|
| Input | capture ring, BS.775 downmix | symphonia decode; **first two channels kept, no downmix** (`analyze/decode.rs:42-44`); same decimation (`:117-126`) | as `drive_with` | none: `synth_features` / `synth_features_accented` (`loop_driver.rs:24-113`) |
| NaN guard, trim, sync delay | yes (`mod.rs:1598-1633`, `:1685`) | no (`analyze/mod.rs:94-106`) | no | n/a |
| Hop chain | `process_hop` | **same** `process_hop`, defaults only, `BandScale::Db` (`analyze/mod.rs:80-107`) | same | not run |
| Clock | `samples_consumed/rate` | `(base + 512)/rate`, identical (`analyze/mod.rs:101-102`) | same; `u.time = shader_time(ts)` (`scene_renderer.rs:700-701`) | `u.time = frame·dt` (`loop_driver.rs:367`) |
| Interpolator, counter latch | yes | no: raw hop frames and `*_fired` flags | no: one render per hop; `beat` is the hop's own value | n/a: pulses are arithmetic (`loop_driver.rs:47-65`) |
| Mirror, frame_prep, frame_graph | yes | n/a | **same functions** (`scene_renderer.rs:706`, `:764`, `:628`) | mirror + frame_prep + render (`loop_driver.rs:370-382`) |
| Bindings | `evaluate` | n/a | `evaluate_offline` + `apply_outputs` (`scene_renderer.rs:719-729`) | **none** |
| Audio textures | waveform from the recording ring; mel columns accumulated | n/a | waveform from the decoded mono; one mel column per tick, `scroll_phase = 0` (`scene_renderer.rs:708-717`; `driver.rs:141-172`) | **not uploaded** |
| trama, Dissolve | yes | n/a | skipped; Dissolve plays as Cut (`scene_renderer.rs:13-15`, `:631-635`) | n/a |

Equivalence is pinned at the analysis level. The golden-vector test drives the real `audio_thread` through
the ring (`mod.rs:1879`), and the offline driver is asserted "against the *same* pinned vector"
(`analyze/mod.rs:296-310`). That ties both paths to one truth rather than to each other. The headless renderer
ticks at the hop rate because "binding `smooth` is a per-frame EMA and `burst_on_beat` is level-triggered"
(`headless/driver.rs:6-9`). The output is H.264 at 30 fps, muxed by ffmpeg with the song slice
(`driver.rs:43-66`). Loop export is deliberately not an analyzer run: "analyzer state … is not exactly
periodic, and routing it in would trade the bit-exact closure" (`loop_driver.rs:79-86`). A GPU test asserts
that frame 0 and frame N are bit-identical for every phase-locked effect (`loop_driver.rs:392-401`).

---

## 8. Design principles and weak points

### 8.1 Principles the code states

1. **One chain in one order, with no wall clock or locks inside it**, so live and offline share it (#2027;
   `hop.rs:1-13`).
2. **The sample clock is the analysis clock** (#1456; `mod.rs:1566-1568`), and the render side interpolates
   on it (#1459; `interp.rs:10-14`).
3. **Nothing in the hot path waits**: no heap in the capture callback (#50), park and unpark instead of
   polling (#73), `try_lock` for config (#77) (`capture.rs:400-401`; `mod.rs:1499-1508`, `:1575-1577`).
4. **Newest wins**: drop-oldest queues (#59, #103; `inbound.rs:1-8`), and one-frame pulses carried by
   counters (#1976; `mod.rs:111-119`).
5. **One perceptual silence gate**, with hysteresis, read by every detector (#1457, #58;
   `loudness.rs:41-54`).
6. **Non-finite values are stopped at every entry**: samples (#51), binding sources (#46), params (#46)
   (`mod.rs:1598-1605`; `bus.rs:473-477`; `params/store.rs:82-91`).
7. **One schema drives positional policy** (`schema.rs:1-16`); circular values bypass the EMA
   (`:196-213`); positions smooth symmetrically (#1920, `:272-278`).
8. **Append-only ABI** (`features.rs:97-121`; `uniforms.rs:116-135`).
9. **Shared per-frame functions** so that headless cannot drift (`frame_prep.rs:1-9`; `uniforms.rs:255-259`;
   `frame_graph.rs:1-8`).
10. **Base and modulation are kept apart** (#4053, #4055; `params/store.rs:7-22`).
11. **Frame-rate independence**: feedback (#1986), rate integrals (#2984), a wrapped `u.time`
    (`chronoflow.wgsl:35-63`; `rates.rs:1-36`; `uniforms.rs:240-247`).
12. **Every sink gets the flash-limited frame** (#108; `flash_limit.wgsl:1-6`).

### 8.2 Weak points and contradictions found

**In the design:**

- **Transforms depend on the frame rate.** `Smooth` is `s = s·f + v·(1−f)` once per render frame
  (`transforms.rs:28-31`; `bus.rs:464-504`), so the same binding is slower at 144 Hz than at 60 Hz. The
  headless driver admits the coupling and ticks at the hop rate "to stay in the live regime"
  (`driver.rs:6-9`). The live regime is the display rate, though, so offline smoothing matches live only when
  fps ≈ 86 Hz.
- **Particles bypass uniform bindings.** `prepare_effect_layers` hands particles `self.latest_audio`, the
  unbound features (`app.rs:1618`; `frame_prep.rs:67-68`), while fragment shaders get the template after
  `Uniform` bindings (`apply.rs:116`). The burst reads `global.beat` from the bound template
  (`frame_prep.rs:46`). A `uniform.kick` binding therefore moves fragment effects but not particle compute.
- **The latch drops counts and strength.** Two beats between polls produce one 1.0 frame, and `beat_strength`
  is not latched (`mod.rs:1117-1125`).
- **Pulse and frame are unordered.** The counters are bumped (Relaxed) before `tx.send`, with no
  happens-before relation to the queue, so a pulse can be seen one poll before its frame (`mod.rs:1688-1701`).
  Pulses run ~1.5 hops ahead of the continuous features by design (`interp.rs:29-31`).
- **Not every stream is delayed alike.** The waveform texture is the newest raw PCM from the recording ring:
  pre-trim, device rate, not behind the playhead and not held by a negative sync offset (`app.rs:1026-1028`).
  `sync_delay.rs:5-7` says "everything has to wait". The spectrum texture is the newest frame, not the
  playhead frame (`mod.rs:1065-1066`).
- **Rate integrals never wrap.** `rates.rs:143-146` argues that the f32 the shader receives has "the same
  precision `u.time` already has", but `u.time` wraps hourly (`uniforms.rs:240-253`) and the integrals do
  not (`rates.rs:165-172`).
- **Live and offline differ for surround input.** Offline keeps channels 0 and 1 (`decode.rs:42-44`); live
  folds the centre in (#67, `capture.rs:381-383`). `ARCHITECTURE-NOTES.md:154-156` calls it the "same code
  path as live" without this caveat.
- **Loop export ignores bindings and textures.** It renders with no bindings and never uploads audio
  textures (`loop_driver.rs:356-385`), so `spectrum()` reads its zero-initialised state there
  (`audio_textures.rs:103-123`).
- **Small per-frame allocations remain.** The waveform and spectrum uploads allocate a `Vec` each frame
  (`audio_textures.rs:155`, `:174`), and WASAPI allocates per packet (`wasapi_capture.rs:359`, `:364-371`).
  This contrasts with the no-allocation stance elsewhere (#75, #150).

**Stale comments and docs (the code wins):**

- `uniforms.rs:7` and `loader.rs:160-165` say 400 bytes; the struct is 448 (`uniforms.rs:336-344`).
- `loader.rs:164`, `:254`, `:281-282` and `uniforms.rs:171-173` still call the audio textures 1×1
  placeholders.
- `loader.rs:257`, `audio_textures.rs:8` and `analyzer.rs:53` say the spectrogram is R8Unorm; it is
  R16Float (`audio_textures.rs:80-86`).
- `audio_textures.rs:59-62` says columns arrive at "~43 Hz" for a "~80–100 ms" time constant. Columns arrive
  at the 86 Hz hop rate, where α = 0.4 gives τ ≈ 23 ms.
- `interp.rs:24-25` says the audio thread "sleeps 10 ms"; it parks until a push (#73, `mod.rs:1575-1591`).
  Bursts are still real (Pulse fragments are 2 hops long).
- `interp.rs:82` gives `AudioFeatures` as 244 B; it is 340.
- `schema.rs:399` says band-pan smoothing differs "from `pan`'s ar(0.03, 0.15)"; `pan` is ar(0.08, 0.08) at
  `:279`.
- `hop.rs:296-299`, `schema.rs:129-131` and `features.rs:25` still describe the kick as a P95-normalised
  30–120 Hz flux. It is the tree ensemble (#3624, `kick_model.rs:1-26`).
- `analyze/mod.rs:38-39` says 13 lagging `pre_norm` fields; `hop.rs:84-90` lists 15.
- `beat/tempo.rs:471` says "~16Hz"; 86.13/6 ≈ 14.4 Hz, as `beat/control.rs:15` says.
- `docs/TECHNICAL.md:122` says shaders define `effect(uv)`; the pipeline calls `fs_main`
  (`gpu/pipeline.rs:219`).
- `docs/TECHNICAL.md:124` lists four libraries; there are six (`loader.rs:95-102`).
- `docs/TECHNICAL.md:138-164` says "74 total", types `frame_index` as `u32` (it is `f32`) and lists `spread`
  and `flux_raw`, which do not exist in the block (`loader.rs:166-249`).
- `docs/TECHNICAL.md:350` and `:357` describe cpal capture and the Legacy linear/dB band scaling. Native
  loopback is tried first (`mod.rs:244-260`), and `Db` is the default (`analyze/mod.rs:85-88`).
- `docs/TECHNICAL.md:231` says particle compute gets 10 audio fields; `update_audio` copies about 30
  (`system.rs:1920-1956`).
- `docs/AUDIO-FEATURES.md:89` says loudness measures "a mono downmix"; it sums K-weighted L²+R² (#56,
  `loudness.rs:216-221`). `docs/AUDIO-FEATURES.md:349` gives one −55 LUFS threshold; the gate opens at −55
  and closes after 250 ms below −60 (`loudness.rs:44-54`, `:235-250`).
