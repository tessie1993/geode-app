# 01: fosfora's audio analysis core (capture → features), for Geode

Fosfora @ `2e85d3b`. Fosfora paths are relative to `crates/fosfora-app/src/audio/` unless they start with `src/`
(= `crates/fosfora-app/src/`). Geode paths are repo-relative. The numbers marked `ranging_compare.py`, `fft_cost.py` and
`f32_mean.py` came from small throwaway scripts written for this study (not committed).
Beat, downbeat, structure and the render-side interpolator belong to other reports; they appear here only
where they plug into the chain.

## 1. How it works

```
capture callback (cpal / Pulse / WASAPI)     no DSP; downmix 5.1→L/R (downmix.rs, BS.775) straight into the ring
  └► RingBuffer: 65536 AtomicU32 slots, SPSC, Release/Acquire on write_pos, unpark the reader   capture.rs:95-180
"fosfora-audio" thread  (mod.rs:1510 audio_thread)
  read → NaN/Inf := 0 (:1601) → input meter, pre-trim (:1607) → mono = (L+R)/2 → recording mirror
  → trim gain (analysis only, :1626) → decimate ×½ⁿ if > 88.2 kHz (decimate.rs) → FIFO
  → while FIFO ≥ 512: timestamp = samples_consumed / rate (sample clock, :1652)
       try_lock config (never blocks, :1502/:1661) → HopAnalyzer::process_hop (hop.rs:200)
       → sync delay (sync_delay.rs) → pulse counters (AtomicU32, :1690) → drop-oldest bounded(4) (src/inbound.rs)
render thread: latest_features(dt) drains to newest, interpolates at a 1.5-hop playhead, counter-latches
  beat/downbeat/drop, decays on a stall by each field's DecayPolicy (mod.rs:1049-1145, :1435)
```

`process_hop` is the canonical chain, in the one order that is correct (hop.rs:1-13, :200-526):

| # | Stage | Algorithm and constants | Why (from the code) |
|---|---|---|---|
| 1 | DC strip | one-pole `y = x − x₁ + R·y₁`, R = exp(−2π·5/sr), on mono, L and R (hop.rs:35-68, :211-220) | DC leaked into sub_bass through the Hann main lobe and flattened zcr (#55). 10 Hz moved the beat-grid lock by a beat on the bench; 5 Hz scores like no filter |
| 2 | Multi-res FFT | a 4096 shift register; Hann (symmetric, `/(N−1)`) and complex rustfft at 4096/1024/512 every hop, 87.5 % overlap at 4096; magnitude × 2/Σw so a full-scale sine reads 1.0 (analyzer.rs:9-12, :65-130, :441-461) | Each band uses the resolution that suits it: 10.8 Hz/bin for sub/bass/kick, 43 for mids, 86 for highs. The coherent-gain fix is #54 |
| 3 | Bands | `Db` map: RMS of bins, then dB over a −60..0 window, +3 dB/oct tilt above 2 kHz, **−6.02 dB compensation** (analyzer.rs:41-48, :576-612). `rms` comes from the last 2048 samples (:616) | One comparable family for the normaliser. The −6.02 dB keeps the structure detectors' old calibration after #54 (the drop warning fell from 68 to 57 of 141 Harmonix drops without it; test `db_band_map_keeps_pre_rescale_calibration`, :910) |
| 4 | Shape | centroid and bandwidth as the mean and spread of power-weighted **log2 f**, 40 Hz–18 kHz → 0..1, 4 octaves = 1 (f64 sums, :740-761); rolloff 85 % and zcr on the same log axis (#62, :819-859); flatness = Wiener entropy over 26 mel bands (:234); flux = half-wave log-magnitude rise with floor 2e-3 (:768-783) | Perceptual faders that don't depend on the sample rate. The flux floor makes flux measure change, not level |
| 5 | MFCC, mel, textures | 26 triangular mel filters → ln → DCT-II ×13 (:304-332, :715); a separate 64-band mel column and a 512-bin log spectrum (peak per bin) for textures (:524-568) | Texture streams ride in `AudioFrame` beside the ABI (mod.rs:59-70) |
| 6 | Kick | gradient-boosted trees over 11-band log-flux plus relative level and low-span flatness/peakiness, 3 hops of context, no lookahead (kick_model.rs:1-25); the threshold maps to 0.5 (`kick_from_probability`, :131) | The old self-normalised 30–120 Hz flux read basslines as kicks (precision .35–.49). Held-out MDB Drums F1 .71 against .51 |
| 7 | Loudness + **the silence gate** | BS.1770: K-weighting per channel (libebur128 biquads re-derived at the device rate), L²+R² summed, running f64 sums over 400 ms (M) and 3 s (S); −60..0 LUFS → 0..1; trend = clamp(M−S, 0, 8 LU)/8. Gate opens at ≥ −55 LUFS and closes after 250 ms below −60 LUFS; it starts closed (loudness.rs:21-54, :101-133, :216-286) | The mono downmix read 3–6 dB low and read anti-phase as silence (#56). A single threshold fluttered on quiet input and dropped the beat grid (#58). **One flag gates every detector** |
| 8 | Stereo | pan, M/S width and Pearson correlation over a 2048-frame window (f64), gated on its own stereo energy (−60 dBFS) so anti-phase still reads; per-band pan from **one packed FFT** `z = l + i·r` (stereo.rs:19-23, :149-251) | Two channel spectra for the price of one transform |
| 9 | HPSS | Fitzgerald median filters on the 1024 spectrum: time median over 17 hops (~0.2 s), frequency median over ±8 bins, Wiener masks, energies mapped to −120..0 dB (hpss.rs:1-38, :95-140) | Raw masked power (~1e-7) fell under the normaliser's 1e-6 span epsilon on real music and zeroed `percussive_energy` |
| 10 | Pitch | YIN over the raw 4096 window, W = 2048; the cross term comes from one packed **f64** FFT and its inverse; threshold 0.15; parabolic refinement; 55–1760 Hz → 0..1 on a log scale; held through unvoiced gaps (pitch.rs:1-48) | f64 because `e(0)+e(τ)−2r(τ)` cancels badly (`D_ROUNDING_FLOOR` 1e-10) |
| 11 | Contrast, dMFCC | top/bottom 2 % per octave from 200 Hz up, 0..60 dB (analyzer.rs:798); a 5-hop trailing regression slope, timbre_flux = L2 over coefficients 1..12 (timbre.rs:17-21) | Contrast is tonality. dMFCC is timbre motion orthogonal to level |
| 12 | Chroma, key | Gaussian kernels per semitone (σ 0.5 st, MIDI 36–96) on the 4096 spectrum, with circular tuning estimation (±50 cents); visual chroma: harmonic template, L∞; key chroma: pure fold from MIDI 54 up plus a gated bass-root tracker (chroma.rs:1-110). Key: braw profiles (Faraldo), Pearson against an energy-weighted mean with τ = 20 s, a switch only after a +0.05 margin held for 3 s (key.rs:1-110) | Bottom octaves alias at 4096. Bench-chosen: braw .484 against Krumhansl-Kessler .421 on GiantSteps |
| 13 | **pre-norm snapshot** | `pre_norm = raw` *before* normalisation (hop.rs:326-332) | Structure, downbeat and offline re-normalisation need true dynamics. Caveat: 15 beat/structure fields in it are one hop stale (:84-90) |
| 14 | Normalise | per `NormPolicy` (below) | |
| 15 | Beat, downbeat, structure | fed from the raw spectra and `pre_norm`, under the same gate (hop.rs:340-472) | (other reports) |
| 16 | Smooth | asymmetric EMA per slot, α = 1−exp(−dt/τ), dt = 512/sr fixed (smoother.rs:20-43) | |
| 17 | Frame | features + spectrum + mel + dmfcc + timestamp + `phase_frozen` (the gate) + beat/downbeat time (hop.rs:495-516) | |

**Normalisation** (normalizer.rs, ranging.rs). `Adaptive`: a 344-hop (~4 s) ring per slot; P5/P95 by two
`select_nth` partitions instead of a sort (ranging.rs:55-86, test `selection_matches_a_full_sort`);
`(v−P5)/(P95−P5)`; a soft knee above 0.85 that keeps slope 1 at the knee and tends to 1 (:139-148); a span
under 1e-6 reads 0. Under the gate the window is **frozen** (nothing pushed) and the output is 0 (:87-96).
`FixedRange`: clamp, hold the last value under the gate. `ZScore` (MFCCs): EMA mean and variance with α
0.01 (~1.2 s), `0.5+0.5·tanh(z/3)`, 0.5 under the gate. `Passthrough`: the producer owns the range.

**Sample rates.** The hop stays 512 samples at every rate, so the hop rate is 86.13 Hz at 44.1 kHz and
93.75 Hz at 48 kHz. Every constant counted in hops is carried to the running rate by `rescale_hops` and
`rescale_decay` (mod.rs:1467-1490, #53). Above 88.2 kHz the input is halved, and halved again, by a
63-tap Blackman half-band filter until it falls in 44.1–88.2 kHz (decimate.rs), "so the chain only ever
sees the rates it was tuned at". The 96 kHz test checks bands against 48 kHz within 0.3 dB (mod.rs:1902).

**Threads and locks.** The capture callback only converts and pushes (`push_iter`, no heap, #50). The
analysis thread parks until the next push wakes it (#73). Config arrives through `try_lock` once per hop;
a busy lock keeps the last snapshot (#77). Nothing else locks in the hot path. Frames reach the render
thread through a drop-oldest queue of 4. One-frame pulses survive drops as atomic counters.

**CPU.** Per hop the chain runs three complex FFTs (4096, 1024 and 512, all on real input, which wastes
half of each), one packed stereo 4096, and a forward plus inverse f64 4096 for YIN. On top come 513 × 2
small medians (HPSS) and about 12 selections over 344 values. That is about 1.06 Mflop of FFT per hop,
**≈ 99 Mflop/s at 48 kHz** (`fft_cost.py`). This is a desktop budget.

## 2. Contracts and invariants

- **ABI.** `#[repr(C)] AudioFeatures` holds 85 `f32` (features.rs:11-124) and is pinned at 340 B
  (`size_is_340_bytes`, :228). Fields are only ever appended, in batched bumps (v2 #1505, v3 #1629,
  band-pan #1801, v4 overlay clock, #81 tempo trust), so every existing index stays put
  (`field_order_first_and_last`, :198). Units are mostly 0..1. The exceptions are raw counts, and
  `bpm` = BPM/300 needs accessors because `60/bpm` once ran 300× slow (#2054, :126-150).
- **Schema.** `FEATURES: [FeatureDef; 85]` rows carry name, `NormPolicy`, `SmoothParams{attack, release,
  bypass}`, `DecayPolicy{Scale, Hold, ForceZero}` and `InterpPolicy{Lerp, Hold}` (schema.rs:20-118). A
  compile-time assert fixes the length (:509). The tests pin the layout boundaries
  (`schema_matches_struct_layout`, :548), **every slot's norm, decay and interp policy by index or name**
  (:654, :674, :706), that circular fields bypass the smoother (`dominant_chroma`'s B→C sweep, :522),
  and **symmetric smoothing on bipolar fields** (`pan` alternating 0.2/0.8 averaged 0.659 under
  ar(0.03, 0.15), #1920, :617). The tests also admit their own weak spot: a new row defaults to Lerp/Scale
  and passes silently (:702-704).
- **Golden vector.** `golden_signal()` is 4 s of stereo with no `rand`: a 60 Hz sub gated at 2 Hz, 440 Hz
  panned left, 3 kHz bursts panned right, and LCG noise (mod.rs:1748). It runs through the **real
  `audio_thread`** via the ring, so it "cannot pass by construction". 344 hops are expected exactly; hops
  40, 172 and 340 are pinned at 1e-5 on all 85 fields (:1879, :2061). Every re-capture is logged in the doc
  comment with its issue and which fields moved (:2030-2057). An `#[ignore]` harness prints the literal
  (:2122). The **offline driver** checks itself against the same constants (src/analyze/mod.rs:296-320),
  which ties the two paths to one truth rather than to each other.
- **Other guards.** Hop output does not depend on read chunking (mod.rs:1772-1776). A warm hop allocates
  nothing, counted by an allocator (hop.rs:761). NaN/Inf never reach a feature (mod.rs:2007). A DC offset
  doesn't move the features (hop.rs:624). The gate has hysteresis (loudness.rs tests). With sync lead,
  `index + phase` is monotonic (hop.rs:849). The ring is checked under Miri in CI.
- **Structure, checked against Graphify** (`tessie1993/fosforatess` @ 2e85d3b).
  - Callers of `HopAnalyzer::process_hop`: `analyze::drive_with` (src/analyze/mod.rs:104) and five tests in
    hop.rs. The live call in `audio_thread` (mod.rs:1682) is missing from the graph, but reading the file
    confirms it.
  - `FftAnalyzer::bands()` has two callers, `extract_features` and one calibration test. No test ties it to
    stereo.rs's `BAND_EDGES`.
  - By grep, the consumers of `schema::FEATURES` are normalizer, smoother, interp, mod.rs (decay),
    hop_sidecar, beat_model, src/analyze and src/signal/schema.rs (wire names). `bindings/sources.rs` does
    not use it, which confirms the third naming scheme noted in ARCHITECTURE-NOTES.md.
- **Sidecars.** Dev-only JSONL dumps with a header (`{"k":"h","v":2,"sr":…,"hop":512,…}`), enabled by
  environment variable. `create_sidecar` never truncates a file that already holds data
  (sidecar_file.rs:1-30, #4174).

## 3. Excellent and weak

**Excellent.**
1. The **sample clock is the only clock**: hop timestamps are `samples/rate`, and dt is fixed per hop
   (hop.rs:175), so analysis cannot depend on the render rate.
2. **One gate, one definition of silence.** It is perceptual, has hysteresis, and every detector reads the
   same flag. The normaliser *freezes* rather than adapts under it.
3. Rationale lives in the code, with issue numbers and bench deltas on almost every constant. That makes
   any constant safe to question.
4. **Tests guard properties, not values**: chunking, allocation, non-finite input, DC, rate decimation,
   bipolar symmetry, circular bypass. The golden vector covers whatever those miss.
5. Percentile ranging with a window that **ages spikes out**, instead of a decay heuristic (ranging.rs:1-9).
6. Cheap tricks used where they count: the packed two-real FFT, selection instead of a sort, a precomputed
   log2 per bin (#76), scratch preallocated everywhere (#75).

**Weak.**
1. **The golden vector barely covers rhythm**: the 4 s clip "never locks" (mod.rs:2032, :2054), so beat,
   downbeat and structure values are near-trivial there. The bench covers them instead.
2. **Producer-side normalisation** pushed the ABI into exceptions (raw counts, `bpm/300`), forced a
   `pre_norm` side channel with 15 stale fields (hop.rs:84-90), and forced offline analysis to
   re-normalise.
3. Calibration debt: `DB_BAND_COMPENSATION_DB` keeps old tunings alive. Band edges are duplicated
   between analyzer.rs:576 and stereo.rs:41 with only a comment ("must not drift"), and no test checks
   them. Stale comments: features.rs:25 still describes `kick` as "30-120Hz spectral flux", and
   docs/TECHNICAL.md says 74 features.
4. Wasted CPU: complex FFTs on real input, three windows sliding over the same buffer every hop, f64 YIN,
   per-slot `exp` in the smoother although dt is constant, and 85 percentile windows allocated where 12
   are used.
5. Percentile stretching invents motion on flat material, which fosfora itself argues in stereo.rs and
   schema.rs:393-405 for band pan. Only `SPAN_EPS` = 1e-6 (absolute) guards it.

## 4. Geode today in this area

- **Two clocks inside one engine.** `write()` runs the filter bank per sample and `Rhythm` on a 10 ms hop
  (`core/sound/SoundEngine.cpp:161-181`, `core/sound/Rhythm.hpp:31`, `Rhythm.cpp:40`). `step(dt)` runs
  everything else once per render frame on the newest window: window RMS, Hann, a 2048 FFT (~43 ms at
  44.1 and 48 kHz, `fftSizeFor`, :48-52), levels, descriptors, drums, events, melody (a **second 4096
  FFT**, :595-598), and Harmony and Form (:769-807). The frame's dt comes from the **render clock**,
  clamped to 1–100 ms (`core/viz/RendererFrame.cpp:47-65`), while the audio comes from the ring. The two
  clocks can disagree frame to frame.
- **Frame-rate compensation is done stage by stage.** Flux compares against a stored spectrum about 1/30 s
  old (`SoundEngine.cpp:409-445`). `RelativeLevel` uses a MilkDrop `f0^(30·dt)` (`core/sound/Stages.hpp:13`,
  :32-79). The filter bank keeps a slice's highest punch (`core/sound/FilterBank.hpp:51-91`). Tests: 30
  vs 120 fps `bassAtt` agrees within **0.15** (`core/tests/sound_test.cpp:109-123`). Chunked writes are
  bit-exact (:161-190), which is the export parity.
- **Ranging.** Producer values keep physical or MilkDrop units: relative ~1.0 (soft-limited to < 4),
  bpm in BPM, pitch in MIDI. The consumer ranges them by `Kind` in `kSourceTable`
  (`core/viz/Reactions.cpp:13-92`), which is better than fosfora's producer-side scheme (weak point 2).
  Measured on material held compressed at ±10 %, a Relative source moves a route by a span of **0.09**,
  against 0.94 under fosfora's percentile ranging. On sparse kicks the spans are 0.80 and 0.95
  (`ranging_compare.py`). Both are level-invariant: a −30 dB master reads the same.
- **Silence has four definitions.** `kDigitalSilence` is −80 dBFS window RMS and freezes the means
  (`SoundEngine.cpp:12`, :216-234). `silenceDb` (user tuning, default −60 dBFS) held 0.5 s gives the
  `silence` and `returned` events (:546-571). `SliceBand` freezes below a power of 1e-12
  (`FilterBank.hpp:79`). `core/sound/Silence.hpp` (−42 dBFS peak, 6 s) is the wallpaper's rest rule, kept
  separate on purpose. None of them is perceptual or has a hysteresis band below the open level.
  BS.1770 K-weighting already exists in `core/audio/dsp/Loudness.hpp:27-32`, for export only.
- **No input hygiene in `write()`**: no NaN guard and no DC blocker (`SoundEngine.cpp:161-178`). DC
  matters more here than in fosfora: `sub` covers 20–60 Hz, which is FFT bins 1–2 at 2048/44.1 kHz
  (`bins(20,60)`, :109, :142-148). The Hann main lobe of a DC offset lands exactly there, and the
  filter bank's `kLow` slice {0, 250} has no high-pass (:123-125).
- **Hi-res audio goes in at the source rate.** The chain must not resample (`docs/AUDIO_CHAIN.md:83-91`),
  so a 192 kHz FLAC runs 8 slices × up to 4 biquads per sample at 192 kHz and an 8192 FFT. On a rate change
  `Renderer::writeAudio` rebuilds the engine (`core/viz/Renderer.cpp:74-82`).
- **Float precision.** `SliceBand` updates its ~4 s mean every sample with k ≈ 5e-6 in f32
  (`FilterBank.hpp:65`, :82). Measured, it **stalls about 0.74 % short of the true level**: after a +1 %
  step it reaches +0.26 %, after +0.5 % it reaches 0 (`f32_mean.py`). At 192 kHz k is 4× smaller and the
  dead band about 4× wider.
- **The frame layout has no static guard.** `GEODE_SOUND_SCALARS` is a hand-typed 108
  (`core/sound/SoundFrame.h:25`). There are exactly 108 scalars today, but no `static_assert` on
  `offsetof(eq)` or `sizeof`. The Kotlin test parses the header's names in order
  (`engine/audio-core/src/test/kotlin/dev/geode/engine/audio/SoundFrameLayoutTest.kt:23-32`), and
  `kSourceTable` order is asserted (`Reactions.cpp:97-103`). `lib_sound.glsl` has 114 `SND_*` defines over
  `uSound[27]`.
- **A circular-smoothing bug of fosfora's `dominant_chroma` kind.** Route smoothing is a linear
  attack/release lag on the target (`core/viz/Reactions.cpp:358-365`). Luminance targets even *force* a
  minimum attack and release. A Follow route from `ChordHue`, `KeyHue`, `KeyRoot`, `PitchClass`,
  `ScaleRoot`, `BeatPhase`, `BarPhase` or `PhrasePhase` sweeps the long way round the wheel when the source
  wraps from 0.97 to 0.03. Nothing in the producer smooths those fields (`core/sound/Harmony.cpp:287`,
  `SoundEngine.cpp:682`), so the consumer is where it goes wrong.
- **Surround.** `SampleRing.write` keeps channels 0 and 1 and drops the rest
  (`engine/audio-core/src/main/kotlin/dev/geode/engine/audio/SampleRing.kt:60-62`), so a 5.1 file loses its
  centre channel (vocals) in analysis. Mono is handled: it is duplicated (`SoundCursor.kt:52-55`).
- **v2 design** (`docs/research/sound-engine-v2.md`): `RealFft` (§4, 6–10× expected), multi-window
  512/2048 plus a decimated 4096 every ~50–100 ms (§3), complex-domain onsets, IF pitch, NNLS chroma.
  Budget 0.3 ms per frame (§10). As written, v2 still runs the windows "per frame" (§3 diagram, §5). The
  structure plan's step 4 moves the FFT path to a hop
  (`docs/research/2026-11-05-fosfora-structure-plan.md` §4) and leaves the hop size open (§5 Q1).

## 5. Recommendations

**R1. Golden vector that also locks (plan step 1, deepened).**
- **What:** port `golden_signal` (deterministic LCG, stereo image, sub gate, bursts) and add a second
  25 s signal: a 120 BPM kick with every fourth hit accented, under an A–C♯ dyad (as hop.rs:766-771), so
  Rhythm, Harmony and Form lock. Drive the real `SoundEngine::write`/`step` through a host `FileDriver`,
  in uneven chunks, at a fixed dt of 1/60. Pin about 4 frames × 352 floats at 1e-5. Add a capture mode
  (`golden_test --write`, like `catalogue_test`) and keep a re-capture log in the file header.
- **Why:** fosfora's single 4 s clip never locks, and Geode's tempo, phrase and section values are the
  ones that matter most. A log of each re-capture makes every deliberate change reviewable.
- **Files:** `core/tests/golden_test.cpp`, `core/tests/golden/*.inc`, `.github/workflows/android.yml`.
- **Behaviour change:** no.
- **Test:** the test itself. Determinism domain per v2 §3: pin on the host compiler (x86_64) and allow
  the documented tolerance.
- **Cost:** S–M. **Priority:** P0, ahead of any move.

**R2. Layout and policy guards.**
- **What:** `static_assert(offsetof(GeodeSoundFrame, eq) == GEODE_SOUND_SCALARS * sizeof(float))` and a
  `sizeof` check against `GEODE_SOUND_FRAME_FLOATS`, in `SoundFrame.h`'s C++ consumer. When the schema
  lands (plan step 3), one test per policy column asserts every row by name, as schema.rs:654-731 does,
  and a new row has to name its policies explicitly (no defaults), which closes the hole fosfora admits.
- **Why:** the 108 is typed by hand. The Kotlin test checks names but not C++ offsets.
- **Files:** `core/sound/SoundFrame.h` (or `SoundEngine.cpp`), `core/tests/sound_test.cpp`.
- **Behaviour change:** no.
- **Test:** the asserts compile.
- **Cost:** S. **Priority:** P0.

**R3. Fix circular sources in route smoothing.**
- **What:** mark the circular sources (`PitchClass`, `KeyHue`, `KeyRoot`, `ChordHue`, `ScaleRoot`) and the
  wrapping phases (`BeatPhase`, `BarPhase`, `PhrasePhase`) in `kSourceTable`. In `Follow`, lag them on the
  unwrapped delta (`d = target − out; d −= round(d)`), then wrap. Better still, make `Kind::Circular` a
  schema column (R7).
- **Why:** this is the bug fosfora hit and documented (#dominant_chroma, schema.rs:200-213, :522), and
  Geode has it today at `Reactions.cpp:358-365`.
- **Files:** `core/viz/Reactions.cpp`, `core/viz/Reactions.hpp`, `core/tests/viz_math_test.cpp`.
- **Behaviour change:** yes, only across a wrap.
- **Test:** a Follow route on `ChordHue` stepped 0.95 → 0.05 with attack 0.2 s never reads near 0.5; one
  on `Bass` is bit-identical to before.
- **Cost:** S. **Priority:** P0.

**R4. Input hygiene at the one entry point.**
- **What:** in `SoundEngine::write`, set non-finite samples to 0 and run a one-pole 5 Hz DC blocker per
  channel before the ring and the filter bank (hop.rs:42-68). Use the same coefficient formula, derived
  at the running rate.
- **Why:** live, export and the Studio preview all pass through `write()`. Geode's `sub` is two bins, so
  DC lands right in it. One NaN poisons every biquad and running mean for good (fosfora #51).
- **Files:** `core/sound/SoundEngine.cpp`, `.hpp`.
- **Behaviour change:** yes, but negligible on clean files: the golden vector moves only within its
  tolerance, or is re-captured once with a log entry.
- **Test:** ports of `dc_offset_does_not_reach_the_features` (`sub`, `bass`, `rms`, `centroid` within
  1e-3 with a 0.3 offset) and `non_finite_samples_never_reach_the_features` (every frame field finite after
  64 NaN/±Inf samples).
- **Cost:** S. **Priority:** P1.

**R5. RealFft first, then the hop (orders plan step 4 against v2 stage 1).**
- **What:** land v2's `RealFft` *before* moving the FFT path onto the hop.
- **Why:** FFT flops per second (`fft_cost.py`):

  | Design | FFT load |
  |---|---|
  | today at 60 fps | 21.5 Mflop/s |
  | today at 120 fps | 43 Mflop/s |
  | today's FFTs moved to a 10 ms hop unchanged | 35.8 Mflop/s (+67 % at 60 fps) |
  | v2's real FFTs (512 + 2048 per hop, decimated 4096 every 5th hop) at 10 ms | 9.2 Mflop/s |

  So the hop becomes cheaper than today only once RealFft exists. Fosfora's ~99 Mflop/s shows what not to
  copy (§6).
- **Files:** `core/sound/RealFft.*`, then `SoundEngine.*`.
- **Behaviour change:** none for RealFft (oracle-checked). The hop is R6.
- **Test:** v2 §4's oracle, plus R1 golden unchanged within its tolerance.
- **Cost:** M. **Priority:** P1 (sequencing).

**R6. HopAnalyzer on Rhythm's 10 ms hop (answers plan §5 Q1).**
- **What:** one analysis hop = `lround(sr × 0.01)` samples, the hop `Rhythm` already runs on. Inside
  `write()`, every completed hop runs the whole chain in a fixed order: filter-bank slice → spectra →
  levels → descriptors → drums → events → Rhythm → melody → Harmony → Form → Emotion → frame. Heavy stages
  run on deterministic sub-multiples of the hop count: the decimated long window every 5th hop (50 ms,
  inside v2's 50–100 ms), and melody every hop or every 2nd hop, decided by measurement. Every time
  constant moves from `perFrame`/dt to the fixed hop dt. Means updated per hop also remove the f32 stall
  of §4 (k ≈ 2.4e-3 instead of 5e-6). `step(dt)` keeps no analysis state and becomes the interpolator (R7).
  Config and the coming `geode_tempo_*` taps go through a mailbox drained once per hop, without blocking
  (fosfora `try_lock_now`, mod.rs:1502).
- **Why:** F1 of the plan. The frame-time dt from the render clock goes away (`RendererFrame.cpp:50`),
  along with the per-stage frame-rate compensations (kFluxLag, slice-peak punch, `perFrame`), which become
  plain hop maths. A fixed hop in *time* rather than in samples is what Rhythm is tuned on, and it makes
  fosfora's `rescale_hops` machinery unnecessary.
- **Files:** `core/sound/SoundEngine.*`, `Stages.hpp`, `FilterBank.hpp`, `Rhythm.*`, `Harmony.*`,
  `Form.*`, `core/viz/Director` (Emotion), `RendererFrame.cpp`.
- **Behaviour change:** yes, deliberate and once.
- **Test:** the same song at 30, 60 and 120 fps gives **bit-identical hop frames**, a stronger test than
  today's 0.15 tolerance. Chunking stays bit-exact. The golden vector is re-pinned once with a log entry.
  The CPU per frame is measured on device against v2's 0.3 ms.
- **Cost:** L. **Priority:** P1.

**R7. Interpolation policies that Geode actually needs.**
- **What:** add a schema `interp` column with five policies:
  - `Lerp`
  - `Hold`: `scale`, `sectionState`, `sectionId`, `keyMajor` as a flag
  - `Circular` (shortest-arc lerp): the hues and pitch classes of R3
  - `Phase` (advanced locally and reconciled, as fosfora's interp does): `beatPhase`, `barPhase`, `phrasePhase`
  - `Clock` (unwrap at `kTimeWrap` 3600 or `kCountWrap` 4096 before lerping): `*Time`, `beatCount`,
    `barCount`, `phraseIndex`

  Triggers take **the maximum over every hop since the last frame**. That keeps Geode's 0..1 strength,
  where fosfora's counter latch flattens it to 1.0 (mod.rs:1117-1142). It also keeps `kick2`/`kick4`
  correct when a 30 fps export frame spans three hops.
- **Why:** fosfora's two policies, Lerp and Hold, are not enough for Geode's frame. Geode's clocks wrap,
  and fosfora's don't.
- **Files:** the schema `.inc`, `core/sound/drive/FeatureInterpolator.*`, `core/viz/ReactSignals`.
- **Behaviour change:** part of R6.
- **Test:** a 16th-note hat at 180 BPM rendered at 30 fps loses no triggers against the hop count. A
  clock across its wrap never steps backwards. A hue across 1 → 0 never reads near 0.5.
- **Cost:** M. **Priority:** P1, with R6.

**R8. One perceptual silence gate.**
- **What:** a `SilenceGate` in the hop chain that reuses `core/audio/dsp/Loudness`'s K-weighting:
  momentary 400 ms, L²+R² summed, running sums in f64. It opens at `silenceDb + 5` and closes after
  250 ms below `silenceDb`, so the user's −60 default gives fosfora's −55/−60 pair. Every stage reads its
  one flag. `RelativeLevel` freezes on it in place of `kDigitalSilence`, and `silence` and `returned` fire
  on its edges. `Silence.hpp` stays as it is, for surfaces that draw nothing.
- **Why:** one meaning of silence, perceptual weighting, anti-phase content not read as silence, and no
  flutter on quiet mic input (fosfora #56, #58).
- **Files:** `core/sound/SilenceGate.hpp`, `SoundEngine.cpp`, `Stages.hpp`.
- **Behaviour change:** yes, small.
- **Test:** ports of `gate_does_not_flutter_on_input_hovering_near_the_threshold` and
  `channels_sum_in_power` (anti-phase as loud as in-phase), plus a −30 dB classical master that never
  trips the gate.
- **Cost:** M. **Priority:** P1.

**R9. Property tests from fosfora that Geode lacks.**
- **What:** a counting `operator new` around warm `write`/`step` calls (zero allocations after 15 s; as
  hop.rs:761), plus the DC and NaN tests of R4.
- **Why:** the GL thread must not allocate, and today nothing checks that it doesn't.
- **Files:** `core/tests/sound_test.cpp`.
- **Behaviour change:** no.
- **Cost:** S. **Priority:** P1.

**R10. Decimate hi-res input before analysis.**
- **What:** halve with a 63-tap Blackman half-band filter (decimate.rs) while the rate is above 88.2 kHz,
  in the driver in front of `write()` (analysis only; playback stays bit-perfect).
- **Why:** it bounds the per-sample filter-bank cost (4× at 192 kHz) and the f32 dead band, collapses
  `fftSizeFor` to one size, and means constants are tuned at only two rates. It fits v2, which already
  decimates for the long window.
- **Files:** `core/sound/Decimator.hpp`, `SoundEngine.cpp`.
- **Behaviour change:** yes, for 88.2–192 kHz sources only.
- **Test:** a port of `high_rate_input_analyzes_like_its_decimated_rate`: 96 kHz against 48 kHz,
  relative levels within 0.3 dB and an equal hop count.
- **Cost:** M. **Priority:** P2.

**R11. Percentile ranging as an opt-in transform, never the producer default.**
- **What:** `Transform::Ranged` (P5/P95 over ~4 s, `select_nth`, knee 0.85, frozen under the gate), offered
  in the mod-matrix transforms (plan step 7). The DNA may choose it for compressed genres. Its span floor
  should be relative (e.g. span < 5 % of P95 reads 0.5), not fosfora's absolute 1e-6.
- **Why:** a span of 0.09 on compressed material (§4) is why EDM can look flat. `bass ≈ 1` stays the
  MilkDrop contract that presets and `lib_sound` read.
- **Files:** `core/modulation/` (future), `core/viz/Reactions.cpp`.
- **Behaviour change:** only for routes that opt in.
- **Test:** a compressed signal ranges to a span above 0.8, and a constant signal reads 0.5.
- **Cost:** M. **Priority:** P2.

**R12. `sound_dump` with fosfora's file rules.**
- **What:** JSONL whose header carries `{v, sr, hop, names[]}` taken from the schema, one record per hop,
  and a never-overwrite open (sidecar_file.rs).
- **Why:** feeds the bench (plan step 8) and R1's capture without guessing the layout.
- **Files:** `tools/` or `core/tests/sound_dump.cpp`.
- **Behaviour change:** no.
- **Cost:** S. **Priority:** P2.

**R13. Fold surround into the pair.**
- **What:** BS.775 Lo/Ro (centre and surrounds −3 dB, LFE dropped) in `SampleRing.write` when there are
  more than 2 source channels (downmix.rs). Later in native, per ADR 0003.
- **Why:** 5.1 analysis currently drops the vocals.
- **Files:** `engine/audio-core/.../SampleRing.kt` and its test.
- **Behaviour change:** yes, for 5.1 and 7.1 only.
- **Cost:** S. **Priority:** P3.

## 6. Do not copy

- **Producer-side normalisation to 0..1** (`NormPolicy::Adaptive` inside the chain). It forced raw-count
  exceptions, `bpm/300` (#2054), the stale `pre_norm` side channel, and offline re-normalisation. Geode's
  physical producer values with consumer `Kind`s are the better contract. Keep them and put the policy in
  the schema.
- **Three complex FFTs plus f64 YIN every hop** (~99 Mflop/s). v2's RealFft with 512/2048 and a decimated
  long window, plus harmonic-sum and IF pitch, covers the same needs at about a tenth of the cost.
  **Don't add YIN**: v2 settled on IF and parabolic refinement.
- **HPSS median filtering** (513 bins × two medians per hop), and **per-band stereo pan** (another 4096
  FFT, no consumer, phone speakers). NoteResponse's percussiveness and v2's hard/soft onsets cover the
  musical need.
- **A fixed hop in samples with `rescale_hops`/`rescale_decay` everywhere.** A hop fixed in time (R6)
  removes the need.
- **Calibration-preserving constants** such as `DB_BAND_COMPENSATION_DB`. Re-tune against a bench (plan
  step 8) instead of carrying old scales forward.
- **The kick tree ensemble as shipped**: Demucs-labelled FMA training data and its pipeline. The *feature
  idea* (11-band flux and level, low-band flatness and peakiness, 3-hop context, no lookahead) is a good
  candidate input for v2 §6's tiny plain-C++ models, trained on our own data under v2's licence rules.
- **The desktop capture stack**: cpal, Pulse and WASAPI backends, the reconnect watchdog, the ALSA/JACK
  noise suppression, the input-trim UI, OSC pulse counts. Geode's tap, ring and epoch already do that job
  on Android.
- **Two-policy interpolation** (Lerp/Hold) and **counter latches that drop trigger strength**. Geode's
  frame needs the five policies and the max-over-hops trigger rule of R7.
