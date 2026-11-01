# Sound engine v2: design for review

Status: **proposal, for the owner's review before any code**. Builds on `core/sound` as it is on
`claude/geode-finish` (FFT, `Rhythm`, `Harmony`, `Form`, `Emotion`) and on the auto-director
(`auto-director.md`). Everything here is engine logic, so it is C++ in `core/` (ADR 0003); Kotlin only
schedules scans and stores files.

## 1. Decisions (from the owner's grilling, 2026-11-01)

| Area | Decision |
|---|---|
| FFT | Our own fast real-input FFT ("hybrid"), designed from the ideas behind pffft, PocketFFT, Ne10 and vDSP, written clean-room |
| Windows | Several resolutions at once: short for hits, medium for the spectrum, long for notes and bass |
| Hits | Complex-domain onsets beside today's log flux, so soft pitched hits are heard as well as drums |
| Chroma | Both: constant-Q chroma for harmony, and instantaneous-frequency pitch refinement for the melody |
| Exactness | Detection is exact and repeatable; visual decisions may be random (live), drawn from the play's seed (export, `Renderer::pinDirector`, #210) |
| Latency | Use the output buffer as look-ahead (file playback), predict for mic and capture; plus **pre-scan maps** the user chooses: tracks played, a playlist, a folder, the library |
| Libraries | Study everything; ship only permissive code (BSD, MIT, Apache, ISC). GPL, AGPL and non-commercial work is read as papers and READMEs, never copied or paraphrased |
| AI | Maths live, small models in the pre-scan. Runtime: Google AI Edge (LiteRT) where a model needs it, plain C++ where it is tiny (§6) |
| Defaults the owner left open (recommended options taken) | Played tracks are mapped automatically, other scans are buttons that run while charging; maps live in private app storage keyed by an audio fingerprint, in Settings › Backup, not in Android cloud backup; "Simple ↔ Rich" goes from one clear reaction layer to every layer |

## 2. What the research found (short)

Sources, licences and numbers are in §9. The points that shape the design:

1. **Today's FFT is slow for a hidden reason.** Without fast-math, clang turns every `std::complex`
   multiply into a NaN check with a `__mulsc3` fallback, which stops vectorisation. Real-input packing (2×),
   radix-4 (~1.3×) and a split re/im layout that auto-vectorises (2–3×) should give **6–10×** (estimate, to be
   measured). The engine also runs a 2N pitch FFT every frame, which is the single largest cost.
2. **Harmony's error is its input, not its maths.** A 2048-point FFT has 21.5 Hz bins; semitones are narrower
   than a bin below ~360 Hz, so the bass octave of the chroma is smeared and chord roots are guesses. A long,
   decimated window fixes that.
3. **Soft music is under-heard** because onsets are flux only. Complex-domain (Bello 2004, rectified per
   Dixon 2006) hears pitched onsets; a frequency max filter (SuperFlux, Böck & Widmer 2013) stops vibrato
   firing hits.
4. **Latency can be hidden for files.** The tap writes PCM before the sink, so the ring already holds the
   100–250 ms the listener has not heard. Centre each window on the *presentation* time and long windows cost
   no visible lag. ExoPlayer already measures this delay (`DefaultAudioSink`'s timestamp poller).
5. **Model licences are the trap.** madmom's and Essentia's models are non-commercial; Chordino, aubio,
   BTrack and libKeyFinder are GPL; PESTO is LGPL. Usable: Beat This! (MIT, code and weights), Basic Pitch
   (Apache), SwiftF0 (MIT), BTC chords (MIT), crema (BSD-2).
6. **Tiny models don't need a runtime.** A Korzeniowski-style key CNN is a few thousand parameters; it runs
   in plain C++ with weights in a generated `.inc`, costs nothing in APK, and gives the same bits in CI host
   tests. LiteRT earns its 1–3 MB per ABI only for transformer-sized models (Beat This!, BTC).

## 3. Architecture

```
ring PCM ──► SoundEngine.write ──► Rhythm (10 ms hop, causal)                       live, GL thread
                     │
                     ├──► RealFft 512   (hits: flux + complex-domain, per frame)
                     ├──► RealFft 2048  (spectrum, bands, flatness, IF pitch)
                     └──► decimate ÷4 ──► RealFft 4096 @ ~12 kHz every ~50–100 ms
                                           (bass + treble chroma, CQ kernel, tuning, NNLS)
                                                     │
                     TrackMap (if this track has one) ┴──► Rhythm locks to the beat grid,
                                                           Harmony/Form take keys, chords, sections
                                                           from the map, live values fill the gaps
pre-scan (WorkManager, IO) ──► same C++ analysers over the whole file
                               + models (beats, chords, key) ──► TrackMap file
```

- **One code path.** The pre-scan runs the same C++ analysers as live, plus the models, at file speed. A
  map is an *addition*: without one, live analysis is complete on its own (mic, capture, an unscanned track).
- **Determinism domain.** arm64 builds, same binary: bit-identical between live and export. armv7 and the
  x86_64 emulator are separate domains, and the tests allow for that.

## 4. Stage 1: the FFT (`core/sound/RealFft`)

- **Plan per power-of-two size**, 256 to 8192. Real input of N packed as N/2 complex (even samples real, odd
  imaginary), a complex transform of N/2, then the split
  `X[k] = ½(Z[k] + Z*[N/2−k]) − ½j·W^k·(Z[k] − Z*[N/2−k])`, with the Hann window folded into the packing.
- **Stockham radix-4 passes** (one radix-2 pass when log2(N/2) is odd), ping-pong buffers, no bit-reversal.
- **Split layout** (`re[]`, `im[]`, 16-byte aligned), complex multiply written as real operations, twiddles
  contiguous per pass. Clang vectorises the inner loops 4-wide with no intrinsics. Hand NEON later only if
  the benchmark asks for it.
- **Tables generated, not computed.** Twiddles and windows computed in double by a generator and baked into
  `FftTables.inc` like `ScaleTable.inc`, so bionic's libm can never change the bits.
- **Flags:** `-ffp-contract=off` on `geode_core` (FMA only where written with `std::fma`), never fast-math.
- **Contract kept:** `magnitudes(real, out)` keeps its signature, and gains `spectrum(real, re, im)` for
  phase. No allocation per call.
- **Tests.** KISS FFT (BSD-3), vendored under `core/tests/third_party/kissfft/` (the submodule is gone,
  #207), is the oracle: max relative error < 1e-5 over 256..8192, random and sine inputs, plus a double DFT
  check, a golden-bits test, and a speed benchmark printed in CI.

## 5. Stage 2: the detectors

**Hits (`SoundEngine` + `Rhythm`).**
- A 3-bin frequency max filter on the flux reference frame (vibrato no longer fires hits).
- Rectified complex-domain onset curve from the same spectra (needs the previous two phases).
- Adaptive peak picking: fire when the curve is a local maximum above `δ + λ·median(last ~0.5 s)`, with a
  minimum gap between hits. One hop (10 ms) of extra delay, which look-ahead hides for files.
- New frame values, `onsetHard` (flux) and `onsetSoft` (complex-domain), so styles and the director can react
  to drums and to note changes separately; `onset` stays their combined maximum.

**Beat (`Rhythm`).** Today's autocorrelation tempo, tempo prior and PLP phase stay. Added: a BTrack-*style*
cumulative score that predicts the next beat one period ahead (studied from the papers; BTrack itself is GPL).
For mic and capture the visuals fire on the prediction minus the measured input-to-screen delay.

**Pitch.** Instantaneous frequency from the phase change of the peak bin across one hop gives sub-cent
accuracy for `peakFreq` and the melody note, at no extra FFT. Parabolic interpolation is the fallback.

**Harmony.**
- Bass (40–250 Hz) and treble (250–2000 Hz) chroma from the decimated long window through a sparse
  Brown–Puckette constant-Q kernel at 36 bins per octave, which also measures tuning (circular mean of
  peak deviations, smoothed over seconds).
- Harmonic weighting of peaks (HPCP *idea*: cos² weight across ±⅔ semitone, sub-harmonic summation),
  replacing today's octave and 3rd-harmonic rules.
- NNLS note estimation (Mauch & Dixon 2010, from the paper) every ~100 ms: the largest gain for chords.
- Key: Temperley and EDM profiles (Faraldo 2016, published numbers) beside Krumhansl–Kessler, scored on a
  CENS-style long chroma; chords: templates plus an online HMM with a ~0.5 s fixed lag.

## 6. Stage 3: pre-scan and track maps

**What a map holds** (binary, versioned, ~20–60 KB per track): the audio fingerprint, analyser version,
beat and downbeat grid, tempo curve, key regions, chord timeline, sections with labels, drops and
build-ups, an energy curve, and the per-section feeling. `Form` and `Director` then know a drop is *coming*,
so a look can land on it.

**Models** (all in the APK, no downloads: the app has no INTERNET permission):

| Job | Model | Licence | Runs on | Size (int8) |
|---|---|---|---|---|
| Beats, downbeats | Beat This! (small) | MIT, code and weights | LiteRT, CPU | ~3–5 MB |
| Chords | BTC (or crema) | MIT (BSD-2) | LiteRT, CPU | ~2–4 MB |
| Key | Own CNN, Korzeniowski-style, trained by us | ours | plain C++ (`KeyCnnWeights.inc`) | < 50 KB |
| Melody pitch (optional, also live) | SwiftF0 | MIT | plain C++ or LiteRT | ~135 KB |
| Sections | No model: SSM novelty (Foote) + clustering (MSAF ideas) | ours | C++ | 0 |

**Size budget: 12 MB of APK in total** (LiteRT CPU runtime for arm64 plus the models). If that is too much,
move the models into a Play Asset Delivery install-time pack. The NPU path (Play AI packs) is not used,
since it fails on sideloaded installs. GPU acceleration is optional and never needed for correctness.

**Datasets for our own models** (licences checked before any training run): Lakh MIDI and Slakh2100
(synthetic audio with exact labels), McGill Billboard chords, Harmonix Set (MIT annotations), GuitarSet,
GiantSteps key (annotations only; the audio is third-party). All-In-One (MIT) may be used *offline on a
desktop* as a teacher to label data, never shipped.

**When scans run.** Each track played is mapped in the background while it plays (the decode already
happens). "Scan this playlist / folder / library" buttons queue a WorkManager job: charging only by default,
pausable, with progress, low priority. About 1–3 s of CPU per track (estimate).

**Where maps live.** `filesDir/trackmaps/` (not `analysis/`, which `GeodeContainer` sweeps as a retired
cache). Excluded from Android cloud backup in `backup_rules.xml` and `data_extraction_rules.xml`, carried on
device transfer, offered in Settings › Backup (`BackupFormat` allowlist, as an option), clearable in
Settings › Storage. The privacy policy (`docs/privacy-policy.md` and its copies, `PrivacyPolicyTest`) gains a
line: analysis maps of your tracks are kept in the app's private storage and never leave the device except in
a backup you make.

## 7. Stage 4: latency

- **Files.** Centre every window on the presentation time. The delay comes from ExoPlayer's position against
  the tap's sample counter, re-measured on route changes (Bluetooth adds 150–300 ms). A long window costs no
  visible lag as long as half of it is under the delay; otherwise it falls back to causal.
- **Maps.** A drop, section or downbeat from the map is known in advance, so its look is committed early and
  lands on the beat.
- **Mic and capture.** No future to read: causal windows, beat prediction, and a measured
  input-to-screen delay subtracted from the predicted beat.
- **Export** reads the same rule and the same map, so export equals live.

## 8. "How complex": Reaction complexity (native side)

A process-wide `complexity` (0 Simple .. 1 Rich, default 0.5 = today), next to the macros:
- `ReactionDna` builds every layer as now, each with a rank (its personality's core layer first).
- At render, routes above `ceil(complexity × count)` fade out over half a second, never cut. Simple keeps one
  clear layer (for example kick → zoom); Rich runs every DNA layer and the director's five routes.
- The director's own route count follows the same value (1 at Simple, 5 at Rich).
- API `geode_reaction_complexity_set(float)`; the Customize control comes later (Wave B UI).
Visual detail (geometry, particles, iterations) stays a separate control through `PerformanceSettings` and
the style params.

## 9. Licence ledger

| Project | Licence | Use |
|---|---|---|
| pffft, PocketFFT, KISS FFT, Ne10, FFTS, muFFT | BSD / MIT | Ideas; KISS FFT vendored as a test oracle only |
| Beat This! | MIT (code and weights) | Ship (pre-scan) |
| Basic Pitch | Apache-2.0 | Optional, pre-scan |
| SwiftF0 | MIT | Ship (optional, live melody) |
| BTC chords / crema | MIT / BSD-2 | Ship one (pre-scan); confirm training-data provenance |
| All-In-One | MIT code, weights to confirm | Desktop teacher only |
| LiteRT (Google AI Edge) | Apache-2.0 | Ship (CPU) |
| madmom | BSD code, **CC BY-NC-SA models** | Papers only, no weights |
| Essentia | **AGPL**, models mostly NC | Papers only |
| aubio, BTrack, Chordino/NNLS-chroma, libKeyFinder, QM Vamp | **GPL** | Papers only |
| PESTO | **LGPL** | Not used |
| BEAST, BeatNet weights | To confirm | Not used until checked |

Main sources: Böck & Widmer (DAFx 2013); Bello et al. (IEEE TSAP 2005); Dixon (DAFx 2006); Brown & Puckette
(JASA 1992); Gómez (2006, HPCP); Mauch & Dixon (ISMIR 2010, NNLS); Temperley (2005); Faraldo et al. (2016);
Korzeniowski & Widmer (EUSIPCO 2017); Foote (2000); McFee & Ellis (2014); Stark et al. (2009, BTrack);
Foscarin et al. (ISMIR 2024, Beat This!); Park et al. (ISMIR 2019, BTC); Kim & Nam (2023, All-In-One);
LiteRT docs (developers.google.com/edge/litert).

## 10. Stages, PRs and how each is checked

| # | PR | Done when |
|---|---|---|
| 1 | `RealFft` + tables + vendored KISS FFT oracle + benchmark; engine switched over | oracle error < 1e-5 at every size; golden bits; ≥ 4× faster than today on host; sound and musical tests unchanged |
| 2 | Onsets (max filter, complex-domain, peak picking, hard/soft), IF pitch, multi-window | Unholy piano cover and a drum track: soft hits found, no vibrato hits; frame cost ≤ today's |
| 3 | Decimated long window, CQ bass/treble chroma, tuning, HPCP weighting, NNLS, key profiles, chord HMM | GiantSteps key and a chord set scored in scratch (scores committed, not audio); better than today |
| 4 | Look-ahead alignment and latency measurement | Visual beat matches the audible beat on device (`DEVICE_CHECKS.md`) |
| 5 | TrackMap format, pre-scan worker, storage, Settings, backup, privacy policy | Map written for a played track; scans pause and resume; backup allowlist and policy tests pass |
| 6 | Models: key CNN in C++, Beat This! and chords through LiteRT | Within the 12 MB budget; maps measurably better than the maths alone |
| 7 | Reaction complexity (native) | Simple = one layer, Rich = all, fades never cut; director test covers it |

Budget throughout: the live engine stays under **0.3 ms per frame** on a mid-range phone, including the
director.

## 11. Open questions for the owner

1. Is a 12 MB APK budget for models acceptable, or should they go into an asset pack?
2. Should scanning the whole library also be offered at first run, or only from Settings?
3. Train our own key and chord models (cleaner licensing, weeks of work), or ship BTC (MIT) for chords now?
