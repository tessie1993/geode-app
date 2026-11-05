# 03: Offline paths, accuracy bench, testing, tooling and CI (fosfora @ 2e85d3b → Geode)

Scope: how fosfora shows that offline equals live and that its detectors are accurate, and how the repo keeps itself honest. `fosfora/` is short for a checkout of fosfora at 2e85d3b. I traced the call paths with Graphify (`tessie1993/fosforatess`) and read every quoted file. On the Geode side I compiled and ran the host tests exactly as `android.yml` does, plus four probes of my own. They were throwaway probes (`fps_probe`, `alloc`, `dump_*`), not committed; R2 and R6 below turn them into tests. I did not run any fosfora code.

## 1. How it works

### 1.1 One offline driver, four users

- **`analyze::drive_with(audio, f)`** (`fosfora/crates/fosfora-app/src/analyze/mod.rs:80-107`) is the whole offline driver: there is no class called FileDriver. It takes a `DecodedAudio` (interleaved stereo plus the rate, `analyze/decode.rs:21-30`) and walks complete 512-sample hops. It folds them to mono (`(L+R)/2`, the same fold as the audio thread) and timestamps each hop on the sample clock, `(base+HOP)/sr` (`:102`). It calls the production `HopAnalyzer::process_hop` and hands the full `HopOutput` to a closure. Settings are pinned to defaults ("a measurement tool must not inherit whatever the operator happens to have left in the UI", `:85-90`).
- **Callers** (Graphify `graphify_callers drive_with`, 4 of 4): `drive` (builds the `HopStream`, `:111`), `headless::driver::run` (`headless/driver.rs:162`), `signal::dump_stream` (`signal/mod.rs:299`), and the test `beat_times_land_on_the_click` (`analyze/mod.rs:398`). The live caller of `process_hop` is `audio_thread` (`audio/mod.rs:1682`). Graphify's strict edge list misses that call, so I confirmed it with grep.
- **Decode** (`analyze/decode.rs:1-6`) runs symphonia with no resampling. Detectors are built per rate, and above 88.2 kHz the same `decimate` stage as the live path halves the rate. Mono is duplicated to both channels. More than two channels keeps the front pair.
- **`HopStream`** (`analyze/mod.rs:29-45`) holds `live` (what a binding would have read: normalized and smoothed), `raw` (pre-norm), and hop indices of beats, downbeats and drops.

### 1.2 What the offline driver feeds

- **`--analyze`** writes `analysis.json` (`analyze/report.rs`, `ANALYSIS_VERSION = 1` at `:17`). It adds lookahead the live path cannot have. `structure_offline::segment` places boundaries on the novelty peak. The tests pin that claim: boundary on the seam to within 1 s, A′ recalling A, an energy-rank split above 0.5 where the online version reached 0.118 (`analyze/mod.rs:211-259`). The `--dense` per-hop frames are optional (`report.rs:26-29`).
- **`--dump-schema`** (`analyze/schema_dump.rs`, `CAPABILITIES_VERSION = 2` at `:40`) lists the build's vocabulary, read from the same places the app reads it: the real `.pfx` scan, the real collectors' emitted keys and the catalog enums. A generator checks names against it instead of keeping its own list.
- **`--validate <dir>`** (`analyze/validate.rs:1-31`) deserializes a generated scene through the app's own serde types and `parse_target`. Every problem names where it is and what would have been valid. Exit codes: 0 clean, 1 problems, 2 usage.
- **`--signal-dump`** (`signal/mod.rs:283-350`) runs the live OSC emitter into a `JsonlSink` instead of UDP. Line 1 is a meta record, then one line per event: `{"addr","args":[{"i"|"f"|"s"}],"ts"}`, with floats written as their shortest round-trip decimal (`signal/sink.rs:60-100`). Config is defaults plus CLI flags only, never the user's `signal.json` (`dump_config`, `:265`). The status heartbeat rides the sample clock, so the bytes are deterministic.

### 1.3 Headless renderer: the same code, minus the window

- `headless/scene_renderer.rs:1-15`: `SceneRenderer::step` (`:690-778`) calls the app's own `mirror_audio_features`, `binding_bus.evaluate_offline/apply_outputs`, timeline `feed_beat/tick`, `frame_prep::prepare_effect_layers` and `frame_graph::execute_and_composite`, in `App::update`'s order. Graphify confirms the shared path: `mirror_audio_features` is called by `App::update` (`app.rs:997`), `SceneRenderer::step` (`:706`) and `LoopSession::render_frame_at` (`loop_driver.rs:370`).
- The two places where it re-sequences instead of reusing are preset application and timeline events (`load_preset_for_cue` `:175`, `apply_preset`). They are guarded by an App-parity probe (`parity_and_first_frame`, `:850`, `#[ignore = "GPU"]`). Anything it cannot reproduce (media and webcam layers, Dissolve, splats) is pushed into `warnings` and written to `run.json`, never skipped silently.
- `headless/gpu.rs:43-54` requests exactly the limits `GpuContext::new` requests, not the adapter's maximum. The reason: "judging must predict the app". The test probes ask for `adapter.limits()` (`gpu/test_gpu.rs:36-44`), a superset.
- The flash limiter is off offline (`scene_renderer.rs:110-113`). It is stateful, and a headless frame must be a pure function of its inputs. Live output is still limited on the way out.
- **`--render-scene`** (`headless/driver.rs`) makes two passes over one decode. Pass 1 analyses the song and builds a `SamplingSchedule` (stills at 25/50/75 % of each section, clips at midpoints and drops; `schedule.rs:49-60`). Pass 2 renders every hop, at the hop rate, and reads back only the scheduled ones. Clips are piped to ffmpeg with the song slice. `run.json` records the sections, the cues that actually ran and the warnings (`driver.rs:236-262`).

### 1.4 Loop export

- `docs/loop-spec.md`, `headless/loop_spec.rs`: a versioned `.loop.json`, with unknown versions rejected. BPM snaps to a whole number of frames: `frames = round(bars·4·60/bpm·fps)`, `effective_bpm = bars·240·fps/frames`, and the effective BPM is derived, never stored (`loop_spec.rs:145-171`).
- `headless/loop_driver.rs:24-110`: beat uniforms are pure arithmetic, with one-frame pulses where the index steps and exact sawtooth phases. `audio:"synthetic"` builds accents from those phases and deliberately does not run the analyzer, "analyzer state is not exactly periodic" (`:79-86`). The frames go through the same `mirror_audio_features`.
- Only `loop:"phase_locked"` effects get the exact guarantee. Two gates enforce it:
  1. A plain-CI source lint (`effect/loader/tests.rs:1699`). It forbids feedback, previous-frame inputs and particles, and keeps a denylist of wall-clock tokens (`u.time`, `frame_decay(` and others) over comment-stripped WGSL.
  2. GPU probes, ignored in CI. `phase_locked_effects_render_bit_identically` (`gpu/pass_executor/tests.rs:1738`) requires that changing only the time uniforms changes no byte. `golden_loop_frame_zero_equals_frame_n` (`loop_driver.rs:401`) requires frame 0 to equal frame N bit for bit, frame 0 to repeat bit for bit, the mid-cycle frame to be non-empty, and **wrap smoothness**: the MAD of the N−1 → 0 step must be at most 1.5 × the largest ordinary step + 1 (`:466-488`). The last rule exists because a sawtooth closes exactly and still jolts.
- `frame_rate_independent_trails` (`:513`) renders 1.2 s at 30 and at 120 fps and requires the same image.
- Non-phase-locked effects get labelled best-effort modes only (`--allow-non-loop`, `--crossfade-bars` with warmup). The crossfade lerp is correct only because frames are premultiplied (`docs/loop-export.md`). ffmpeg is a PATH dependency, probed for each codec with an error that says what to install (`loop_encode.rs:18-40`).

### 1.5 The accuracy bench

- **Path** (`bench/README.md`): binary `--signal-dump` → JSONL → `benchlib` scorer, which sees only the dump and the annotation bundle, never engine internals. The dump cache key is sha256(audio) + sha256(binary) + flags (`benchlib/runner.py:1-6`), so "any rebuild invalidates honestly". `require_features` refuses a binary built without `analyze`: it runs `--caps` with a 10 s timeout, because an old binary would launch the GUI instead (`runner.py:37-70`).
- **Metrics** (`benchlib/metrics/__init__.py:21-58`) are mir_eval conventions, echoed into every results file as a `CONVENTIONS` block: beat F at 70 ms with a 5 s trim plus an untrimmed F; tempo Acc1/Acc2, plus lock time and locked fraction for a causal system; key MIREX-weighted; structure boundaries at 0.5 and 3 s; drop policies. An unknown policy env var is fatal, "a silent fallback would make a published number a lie" (`:78-91`). Metrics run only when the annotation fields they need are present (`score_all`, `:112`).
- **Datasets** (`bench/manifests/*.json`, schema `fosfora-bench-manifest/v1`): Ballroom 698 (audio "no explicit license, research use only"; CPJKU annotations), GiantSteps Key 604 and Tempo 664 (annotations "CC-licensed"; audio is Beatport previews, never redistributed), Harmonix 912 (annotations MIT; audio YouTube, behind a mel-DTW alignment gate, with 538 of 912 excluded by reason), SMC 217. Fetch scripts expose `fetch|prep|verify`. Per-track failures are normal and counted. A checksum mismatch is fatal.
- **Published table**: `docs/BENCHMARKS.md` is generated by `bench/report.py` from local results plus `bench/baselines.yaml` (cited numbers, `status: verified|tbd-verify`, `mode: offline|online`, never re-run). Every row carries its mode and source, and coverage and exclusions are tabulated. Fosfora's own numbers are modest and printed as they are (Ballroom beat F 0.549 causal against Beat This! at 0.975 offline).
- **CI fixture**: `bench/make_fixture.py` synthesizes a WAV and its annotations from one score. The ground truth is exact by construction and there is no licence question. The fixture is copied rather than imported from the capture tooling, so its bytes stay stable. `bench/check_fixture.py` asserts the floors and bands in `fixture_expectations.json` ("floors, never exact pins: exact scores are per-binary"), for example beat F ≥ 0.85, exactly 1 drop, key score 1.0 and lock time ≤ 6 s. CI runs it in `ci.yml:225-257`.
- **Rounds discipline** (`bench/manifests/q*_frozen.json`, `bench/q*_verdict.py`). Each detector change is a round with:
  - a split by sha256(track_id) parity (even = tune, odd = holdout, `q4499_verdict.py:39`);
  - targets pre-registered before any arm is read, `frozen_at`, with amendments appended and never edited;
  - candidates forecast by a replica that must reproduce the shipped binary's decisions with zero mismatches (`sweep_*.py --validate`, `sweep_key.py:17`);
  - a selection rule that breaks ties toward fewer new constants;
  - one holdout read;
  - an `on_miss` clause that forbids re-tuning on the holdout.

  `q4499_bar_line_frozen.json` shows it working. Round 1's arm passed every target, then failed the CI fixture's downbeat floor, which was not in the forecast population. That was recorded as a process error and fixed in round 2 by adding the fixture to the forecast.

### 1.6 Generated docs and repo checks

Generated and checked rather than hand-kept:

- `scripts/gen_gallery.py --check` regenerates `docs/GALLERY.md` from the `.pfx` files (a hand-kept count once read 24 when there were 38).
- `scripts/check_docs.py` checks links, anchors, the gallery and the "N audio features" claim.
- `scripts/check_changelog.py` protects the release-body headers.
- `scripts/audit_pfx_rates.py --check`, and `scripts/diagrams.py` (SVG light/dark pairs).
- `build_catalog.py` and `build_thumbnails.py` render through `--render-scene` and never re-implement the grammar.

All run in `repo-checks.yml`, because `ci.yml` ignores `**.md`. `xtask` has one tested scaffold, `new-effect`.

### 1.7 CI (`fosfora/.github/workflows/ci.yml`)

| Job | Lines | Blocks on |
|---|---|---|
| fmt | 32-42 | rustfmt |
| demos | 44-53 | demo presets valid |
| deny | 55-79 | cargo-deny, plus proof that GPL `rusty_link` is absent from the release graph, with a positive control first |
| lint | 81-135 | clippy `-D warnings` once per feature set (default, video, webcam, depth, analyze, link, sinks, release, profiling) |
| xr-lib | 137-156 | clippy for `aarch64-linux-android`, no-default-features and `decode` (type-check only, no NDK) |
| test | 158-200 | `cargo test` on defaults and on the union of all features; ffmpeg installed for the encode tests |
| miri | 206-223 | `miri test audio::capture` on the lock-free ring, nightly pinned by date |
| bench-fixture | 225-257 | benchlib unit tests (79 tests, `test_benchlib.py`) and fixture generate → dump → score → floors |
| build | 259-352 | four targets; lib tests on macOS and Windows "where the platform's float math differs" |

Other details: every action is pinned by SHA. Each job's timeout is about 3× a warm run, and each apt step has a 15-minute cap. `tag.yml` requires CI to pass before tagging. **No GPU job exists.** Every pixel gate is `#[ignore]` and dev-run ("decision #2066: dev-run release probe").

## 2. Contracts

1. **Live equals offline, pinned to one vector, not to each other.**
   - `audio_thread_golden_vector` (`audio/mod.rs:1879`) drives the real `audio_thread` through its ring.
   - `offline_driver_matches_the_live_audio_thread` (`analyze/mod.rs:299`) drives `drive()`.
   - Both assert the same `GOLDEN_HOPS` (hops 40/172/340 × 85 features, |Δ| < 1e-5).

   The input is `golden_signal(sr, secs)`, a fixed-LCG 4 s stereo signal: a 60 Hz gated sub, a 440 Hz mid, 3 kHz bursts, noise, and L ≠ R (`:1748-1771`). Both tests depend on per-hop values being independent of read chunking: "the FFT window is a shift register and the timestamp comes from a pure sample counter" (`:1773-1777`).
2. **Golden re-capture is deliberate and documented.** `capture_golden_vector` is `#[ignore]`d and prints the literal (`:2122`). The doc comment above `GOLDEN_HOPS` keeps a dated changelog of nine re-captures, each saying which feature indices moved and why.
3. **Byte determinism of the dump**: `dump_bytes_are_deterministic` (`signal/mod.rs:369`) compares two in-memory runs over 8 s. `jsonl_lines_are_pinned` (`signal/sink.rs:137`) pins the serialized lines byte for byte. Fixture scores are floors precisely because exact scores move with the toolchain.
4. **Layout guards**: `size_of::<AudioFeatures>() == 340` (`audio/features.rs:229`); `ShaderUniforms == 448` (`gpu/uniforms.rs:340`); `schema_matches_struct_layout` writes i into slot i and reads it back by name (`audio/schema.rs:548`); `uniform_block_layout_matches_rust` **parses the WGSL with naga** and compares offsets against `offset_of!` for both WGSL copies (`effect/loader/tests.rs:187`). There are about 20 more size and offset pins across `gpu/`.
5. **Steady state allocates nothing**: `test_alloc.rs`, a thread-local counting `#[global_allocator]` in the test binary only. `a_warm_hop_allocates_nothing` (`audio/hop.rs:762-795`) asserts 0 allocations over more than 1000 warm hops while still locked.
6. **Pre-registered acceptance tests**: `beat_times_land_on_the_click` (`analyze/mod.rs:342`), median beat-to-click offset within ±25 ms over a synthesized kick track. It also rules out adding a latency constant (the median would move to about −46 ms).
7. **What CI blocks on**: format, clippy for every feature set and target, unit tests (golden, layout, alloc, schedule, loop-spec, phase-locked source lint), Miri on the ring, dependency licences, the fixture floors, and the docs/changelog/gallery checks. **CI does not block on** any GPU pixel test or on the dataset bench numbers. `report.py --check` needs gitignored local results, so it cannot run in CI.

## 3. Excellent vs weak

**Excellent (take the idea):**
- Two paths tied to one pinned vector, not to each other (`analyze/mod.rs:296-297`). A drift in either fails, and the vector also guards refactors.
- The scorer reads only the wire format. The engine can be rewritten without touching `benchlib`, and the same format scores TouchDesigner (`td_compare/`).
- The cache keyed on binary hash, and the `--caps` gate against a wrong-featured binary (`runner.py`).
- A synthetic fixture whose ground truth comes from the same score as its audio. Floors are not pins, and the floors' policy text records the measured values (`fixture_expectations.json:_policy`).
- Rounds: tune and holdout by hash parity, targets frozen first, append-only amendments, a validated replica before any sweep. The recorded miss in round 1 of q4499 is the method working.
- Honesty rules in the generated docs: coverage, exclusions by reason, and cited baselines labelled offline or online (`docs/BENCHMARKS.md`).
- Headless device limits equal to the app's (`headless/gpu.rs:43`). Stateful output stages switched off offline, with the reason written down.
- Wrap smoothness on top of bit-exact closure (`loop_driver.rs:466`).
- Every action pinned by SHA, timeouts that state their basis, a positive control before a negative grep (`ci.yml:70-79`).

**Weak (do not copy as is):**
- Every pixel gate is `#[ignore]`. The golden loop, App parity and phase-locked bit identity never run in CI, so they protect only when a developer remembers them.
- The phase-locked lint is a denylist over raw source. Helper functions have to be added to it by hand (`effect/loader/tests.rs:1726-1729`).
- Doc drift inside the loop code. `loop_spec.rs:49-51` says `Synthetic` runs "through the real analyzer (… loop closure is perceptual)". `loop_driver.rs:79-86` and `docs/loop-spec.md` say it deliberately does not, and that closure is golden. The three disagree.
- The mono fold and the timestamp are written out twice, in `drive_with` (`analyze/mod.rs:97-102`) and in `audio_thread`, kept in step only by the golden vector.
- Frozen manifests point at `/mnt/t9/...` binaries and roots, so a round is auditable but cannot be reproduced by anyone else. The `uv` scripts take unpinned `mir_eval>=0.7`.
- The golden test pins three hops at one 1e-5 absolute tolerance across fields of very different scales.
- The bench takes Harmonix audio from YouTube via yt-dlp, a terms-of-service and reproducibility liability. 538 of 912 tracks are excluded.

## 4. Geode today (facts)

- **Host tests**: these are inline `g++` lines in `android.yml:127-172`, with no script or CMake target to run them locally the same way. `-Werror` is on only for `fluid_math_test`, `resampler_test` and `loudness_test` (`:133,162,164`). I ran all eleven: all pass in 57 s. I got one warning, which CI would not fail on: `core/viz/Lfo.cpp:56` `-Wswitch`, `Retired25` not handled. No sanitizers. Under ASan and UBSan, `sound_test` and `musical_test` are clean in 68 s.
- **There is already an offline driver shape.** `core/tests/sound_test.cpp:46-62` `run()` writes "exactly the audio up to each frame's end, then steps: what the export path does". The chunk end is `llround(i·rate/fps)`.
- **Determinism (probe `fps_probe`)**: `SoundEngine` over fosfora's golden signal ported to C++ (8 s at 60 fps, 480 frames × 352 floats) is byte-identical across runs. `g++ -O1` and `clang++ -O2` on x86-64 give **byte-identical** frames. With FMA contraction (`-march=x86-64-v3 -ffp-contract=fast`), the largest difference is 1.8e-4 and 477 of 168,960 values exceed 1e-5. An arm64 NDK build contracts by default (clang `-ffp-contract=on`, and `GEODE_COMPILE_OPTIONS` in `CMakeLists.txt:12-18` does not turn it off), so a host golden vector is a host contract, not a device one.
- **Frame-rate dependence, measured.** Same signal, compared at t = 8 s: 183 of 352 fields differ by more than 1e-3 between 30 and 60 fps. Some examples: `mid` 0.704 at 30 fps vs 0.630 at 60 (+12 %); `kickEnv` 0.075 vs 0.035 (2.1×); `buildUp` 0.087 at 60 vs 0.178 at 120. `bpm`, `beatCount` and `beatPhase` are identical at every rate (`Rhythm` runs on the 10 ms hop inside `write`, `core/sound/SoundEngine.cpp:161-182`). This confirms gap F1 in the plan (`SoundEngine::step` at `:200`).
- **Steady state is already allocation-free** (probe `alloc.cpp`, a counting `operator new`): 0 allocations over 600 warm frames of `write` + `step`. Nothing locks this in.
- **Export clock**: `OffscreenSceneRenderer.renderFrame` (`engine/scenes/.../offscreen/OffscreenSceneRenderer.kt:135-149`) uses `timeMs = frame*1000/fps` and delivers audio up to `nextTimeMs`, which `TrackAudio.deliverUntilMs` turns into `endMs*rate/1000` frames (`TrackAudio.kt:98-108`). So export frame n hears ⌊⌊(n+1)·1000/fps⌋·rate/1000⌋ samples. That is up to one millisecond (44 samples) short of the `llround((n+1)·rate/fps)` that `sound_test` uses. Frame 0 at 30 fps: 1455 vs 1470. Export and the host harness are therefore not sample-identical today.
- **Export fps can fall back**: `VideoExporter.openEncoder` (`app/.../export/VideoExporter.kt:597-611`) takes the first `EncoderAttempt` the device's encoder accepts, and `totalFrames` and the renderer's fps follow it (`:423-445`). With the frame-locked FFT path, a fallback from 60 to 30 fps changes what the engine hears (see the measurement above).
- **Layout guards**: `SoundFrameLayoutTest` (`engine/audio-core/src/test/.../SoundFrameLayoutTest.kt`) parses `core/sound/SoundFrame.h` and checks the scalar order against Kotlin and every `SND_*` slot in `lib_sound.glsl`. That is a good cross-language text guard. The plan's §2 row F3 credits `glslcheck` for the `lib_sound` check, but this test is what does it (`tools/glslcheck/check.mjs:329-334` only parses the names for `--sound`). **There is no C++ `static_assert`** tying `sizeof(GeodeSoundFrame)` (1408 B, measured) to `GEODE_SOUND_FRAME_FLOATS` (`SoundFrame.h:26`). The one in the reactions table is `Reactions.cpp:103`.
- **Regeneration precedent**: `catalogue_test --write` regenerates `core/tests/catalogue.txt`, and JVM tests read it (`AGENTS.md` "Build, test, CI"). That is exactly fosfora's capture-harness pattern.
- **Loop export** (`app/.../export/LoopRender.kt:40-63`): `LoopSpec(loopMs, crossfadeMs, fps)` is a time-based crossfade loop, which is fosfora's *best-effort* mode. There is no bar snapping and no closure or seam test (`app/src/test/.../export/` has `ChapterMarkers`, `ExportRun`, `MuxerBudget`, `PcmResampler`, `RenderBatch` and `RenderEta` tests only).
- **Offline rendering is device-only**: `OffscreenSceneRenderer` and `TrackAudio` need MediaCodec and EGL. Thumbnails render on the emulator through `StyleThumbnailRenderTest` with a seeded `SyntheticGroove` (`app/src/androidTest/.../thumbs/`), triggered manually (`render-thumbs.yml`). `tools/glslcheck` (one frame per shader, WebGL) and `tools/hostlink` (host link) are in no workflow.
- **Device checks**: `docs/DEVICE_CHECKS.md` is a reconstruction. Items 1-12, 14-15 and 28-36 are unknown.
- **No dataset scoring.** Rhythm, Harmony and Form constants are tuned by ear (plan §2, row F13).

## 5. Recommendations (each with files, behaviour, test, cost, priority)

### R1. A FileDriver in core and one frame clock (step 1). Cost S. P0.

- **Files**: `core/sound/drive/FrameClock.hpp` and `core/sound/drive/FileDriver.{hpp,cpp}`; `core/tests/golden/GoldenSignal.hpp`; `core/tests/wav/WavReader.hpp`.
- **`FrameClock::samplesThrough(frame, fps, rate) = llround((frame+1)·rate/fps)`**: the one definition of what video frame n has heard.
- **FileDriver**:

  ```cpp
  struct DriveConfig { double fps; GeodeSoundTuning tuning = SoundEngine::defaultTuning(); };
  class FileDriver {
   public:
    FileDriver(int rateHz, DriveConfig);
    // Feeds interleaved PCM; for every completed video frame calls onFrame(index, tSeconds, frame).
    // tSeconds = samplesThrough(index)/rate (the sample clock, never a frame-count estimate).
    void feed(const float* interleaved, int64_t frames, int channels,
              const std::function<void(int, double, const GeodeSoundFrame&)>& onFrame);
    void finish(...);  // the tail: partial frames are dropped, as fosfora drops partial hops
  };
  ```

  When step 4 lands, the frame callback becomes a hop callback with the same signature, and FileDriver drives the `HopAnalyzer`.
- **Tuning**: always `defaultTuning()` unless a flag sets it. It never reads `SoundTuning` from prefs. This is fosfora's `dump_config` rule.
- **WAV reading**: dr_wav (public domain / MIT-0, single header) under `third_party/`, or a 60-line PCM16/float32 RIFF reader in `core/tests/wav/`. No MP3 is needed: the bench converts to WAV at prep time.
- **Behaviour change (small, deliberate)**: `OffscreenSceneRenderer.renderFrame` delivers `TrackAudio.deliverUntil(samplesThrough(frame))`. That means passing frames, not milliseconds: expose the rate after the first decode and compute the same formula in Kotlin, pinned by a JVM test against `geode_frame_clock_samples` or against a table copied from the C++ test. This makes export, the host harness and the golden vector hear the same samples. It also replaces `sound_test.cpp:46-62` `run()` with FileDriver.
- **Test**: `frame_clock_test` asserts the cumulative sample sum over an hour at 24/25/30/50/60/120 fps equals `llround(T·rate)`, with no drift. A JVM test asserts the Kotlin clock gives the same numbers.

### R2. A golden vector and determinism (step 1). Cost S. P0.

- **File**: `core/tests/golden_test.cpp`, plus `core/tests/golden/sound_golden.inc`, which is generated: `golden_test --write core/tests/golden/sound_golden.inc`, the same as `catalogue_test --write`.
- **Signal**: `GoldenSignal(44100, 4.0)`, a C++ port of `golden_signal`. Fixed LCG, 60 Hz gated sub, 440 Hz mid, 3 kHz bursts, L ≠ R.
- **Pins**: run FileDriver at 60 fps and pin frames {20, 120, 239} (warming, mid, late), all 352 floats each, printed at `%.9g`. Pin a second set at 30 fps, frames {10, 60, 119}. Fosfora pins 3 hops; pin the whole frame, because spectrum and chroma are where a refactor drifts.
- **Tolerance**: absolute 1e-5 on the 0..1 fields. Relative 1e-6 on `bpm`, `peakFreq` and `pitch`. Exact equality on the counters (`beatCount`, `barCount`, `phraseIndex`, `sectionId`, `sectionState`) and on the categorical fields (`keyRoot`, `keyMajor`, `scale`).
- **Also**:
  1. Two runs `memcmp`-equal, the byte-determinism contract.
  2. Build the test with `-ffp-contract=off` written explicitly. Host x86 contracts nothing today, but this keeps a future `-march` from moving the vector. A header comment states the vector is a host contract and that a device differs by up to about 2e-4 (my FMA measurement).
  3. A doc comment above the `.inc` include listing each dated re-capture and which indices moved, as in `audio/mod.rs:2003-2033`.
- **CI**: one more `g++` line in `android.yml` (better: R6's script).
- **Pre-registered failing test**: `fps_invariance_test` asserts that the same signal at 30, 60 and 120 fps gives the same hop-domain frames. Today it fails (183/352 fields). Fosfora parks the equivalent `beat_times_land_on_the_click` behind `#[ignore]` until the fix lands. Here, check it in reporting only, without failing (print the count, exit 0) until step 4, then make it blocking and re-pin the golden vector once, on purpose. Step 4's PR has to turn this test green.

### R3. `sound_dump` JSONL (step 1). Cost M. P0.

- **File**: `core/tools/sound_dump.cpp`, a host binary, built by `tools/host-tests.sh` and reused by the bench.
- **Usage**: `sound_dump <in.wav> [--out path|-] [--fps 60] [--dense] [--tuning default]`.
- **Line 1** (keys sorted, as fosfora's meta):

  ```json
  {"meta":1,"schema":"geode-sound-dump/v1","frame_floats":352,"frame_version":1,"source":"x.wav","sample_rate":44100,"fps":60,"driver":"frame","tuning":"default","git":"<sha or dirty>"}
  ```

- **Event lines**:

  ```json
  {"t":12.5,"ev":"beat","n":23,"bpm":124.0,"conf":0.91}
  ```

  `ev` is one of `beat`, `downbeat`, `phrase` (from `beatCount`, `barCount` and `phraseIndex` increments, so a trigger is never lost: fosfora's rule F6), `onset`, `kick`, `snare`, `key` (on a change of `keyRoot` or `keyMajor`, with `clarity`), `section` (on `sectionChange`, with `id`, `state` and a `GEODE_SECTION_*` name), `buildup` (threshold crossings) and `drop`.
- **Timestamps**: `t` is the sample-clock time, `samplesThrough(n)/rate`, the end of the frame. Score what the wire says, with no grid-bias correction (`metrics/__init__.py:7-10`).
- **`--dense`**: one `{"t":…,"f":{name:value}}` line per frame, names from `SoundFrameLayout`. Step 3 later generates them from `FeatureSchema.inc`.
- **Number format**: floats through `std::to_chars` (shortest round trip), so 0.82f prints as `0.82`.
- **Tests** in `golden_test.cpp`:
  1. `dump_lines_are_pinned`: meta plus two events, byte for byte.
  2. `dump_is_deterministic`: two runs over 8 s of `GoldenSignal`, bytes equal.
  3. Every beat event's `n` increments by 1.
- **Why JSONL, not CSV**: mixed event kinds, appendable schema, `"schema"` versioning. And it is fosfora's format, so `td_compare`-style tooling and mir_eval loaders port as they are.

### R4. The bench harness (step 8). Cost M (L with rounds). P1.

**Layout**: `tools/bench/`, Python run with `uv` and pinned exact dependencies (`mir_eval==0.8.2`, `numpy`, `soundfile`) through PEP-723 headers.

```
tools/bench/
  benchlib/ dump.py annotations.py metrics/{beats,tempo,key,structure}.py results.py runner.py
  manifests/<ds>.json      checked in: sources, pins, expected counts, exclusions with reasons, licence notes
  fetch_<ds>.py            fetch | prep | verify ; data under $GEODE_BENCH_DATA (outside the repo, gitignored)
  run_bench.py  score_dump.py  report.py (--check)  test_benchlib.py
  rounds/<issue>.json      frozen round manifests (R5)
```

- **Runner**: the cache key is sha256(wav) + sha256(`sound_dump` binary) + flags. The runner refuses a binary whose `--caps` output lacks `frame_version`. Results are sorted, rounded to 4 places, and carry the `conventions` block. Metrics run only when their annotation fields are present.
- **Datasets, in order**. No audio ever enters the repo. Manifests pin archive sha256 and annotation repo commits.
  1. **GiantSteps Tempo** (664 EDM previews; annotations from the GiantSteps GitHub repos, which fosfora's manifest calls CC-licensed; audio is Beatport previews from JKU's mirror, research use, never redistributed). Scores tempo Acc1/Acc2 and lock time. This is Geode's in-genre test.
  2. **GiantSteps Key** (604; same licence situation). Scores `keyRoot`/`keyMajor` with the MIREX weighting (`mir_eval.key`).
  3. **Ballroom** (698, 13 Sturm duplicates excluded → 685; audio "no explicit licence, research use only"; CPJKU annotations). Scores beat and downbeat F, CMLt and AMLt.
  4. **SMC_MIREX** (217, beats only; Creative Commons per its INESC distribution, exact variant to confirm). A hard case for beats.
  5. **Harmonix: annotations only.** Do not fetch audio from YouTube: it breaks terms of service, cannot be reproduced, and fosfora lost 538 of 912 tracks that way. Use it only if a lawful copy of the audio exists.

  Confirm every licence against the upstream repo when the manifest is written. The notes above are fosfora's claims.
- **Baselines**: `baselines.yaml` with cited, never re-run numbers. Reuse fosfora's verified rows (Schreiber 2018, Korzeniowski 2018, Böck 2016, BeatNet 2021), labelled offline or online. Geode is causal and frame-locked. Label it so.
- **CI fixture**: written in **C++, not Python**, so `android.yml` needs no Python packages.
  - `core/tests/bench_fixture_test.cpp` synthesizes, from one score, a 124 BPM, 20-bar, A harmonic minor (Am–Dm–E–Am) arrangement with intro, build and drop. The idea is `make_fixture.py`. The synth code is copied, not shared, so its bytes stay frozen. The same score yields the ground truth.
  - It runs FileDriver and scores with a 40-line F-measure (70 ms window, 5 s trim, the same as mir_eval's).
  - It asserts the floors in `core/tests/bench_fixture.json`: `beat_f ≥ 0.85`, `bpm` within 4 % of 124 after 6 s, `keyRoot == A && !keyMajor` over the last 50 %, exactly one `drop` within one bar of the score's drop, `sectionChange` count ≥ 3.
  - The `_policy` text records the measured values, and the floors ratchet only upward.
  - The Python `benchlib` gets its own cheap CI step later, if wanted.
- **Generated doc**: `docs/BENCHMARKS.md` from `report.py`, with coverage, exclusions by reason, and modes. It is regenerated locally. CI cannot check it, because the results are local; say so in its header.

### R5. The rounds discipline, for every detector change (from step 4 on). Cost process only. P1.

- One `tools/bench/rounds/<issue>.json` per change of a Rhythm, Harmony, Form or Emotion constant: issue, mechanism evidence, the baseline binary hash, the split (sha256(track_id) parity), candidates, targets frozen before any arm is read, a selection rule (ties go to fewer new constants), and `on_miss`.
- Amendments are appended, never edited. The holdout is read once.
- **Include the CI fixture in the forecast population.** That was the lesson of round 1 of q4499.
- Root paths inside the repo or relative to `$GEODE_BENCH_DATA`, never a personal mount.

### R6. CI changes in `android.yml`. Cost S. P0.

- **`tools/host-tests.sh`**: every host test, one compile line each, `-Werror` everywhere (fix `Lfo.cpp:56` `Retired25` first). It also compiles `golden_test`, `bench_fixture_test` and `sound_dump`. CI calls only the script, so local and CI runs are the same command.
- **Sanitizer step**: the same script with `SAN=1` (`-fsanitize=address,undefined -fno-sanitize-recover=undefined`). About 70 s for the sound tests, measured. This is Geode's answer to fosfora's Miri job, because `SampleRing` and the C++ core are the unsafe parts.
- **`core/tests/alloc_test.cpp`**: a counting `operator new` (port `test_alloc.rs`). It asserts 0 allocations in `SoundEngine::write` + `step` after 10 s warm. It passes today (measured 0/600 frames); the test locks that in.
- **Layout guards**:
  - In `SoundFrame.h`, a size check that compiles as both C and C++:

    ```c
    #ifdef __cplusplus
    static_assert(sizeof(GeodeSoundFrame) == GEODE_SOUND_FRAME_FLOATS * sizeof(float), "...");
    #else
    _Static_assert(sizeof(GeodeSoundFrame) == GEODE_SOUND_FRAME_FLOATS * sizeof(float), "...");
    #endif
    ```

    plus `offsetof` pins at the block boundaries (`eq`, `spectrum`, `waveform`, `chroma`), as fosfora's boundary pins at `schema.rs:567-576`.
  - The same for `GeodeSoundTuning` against `GEODE_SOUND_TUNING_FLOATS`.
  - Keep `SoundFrameLayoutTest` as the cross-language check. Fosfora does this with naga; GLSL here is checked by regex, which is good enough while `lib_sound.glsl` is plain `#define`s.
- **`tools/hostlink/hostlink.sh`**: add it as a step (it fetches about 2 MB of projectM headers). It catches NDK-link breaks before `assembleDebug`.
- **`tools/glslcheck --all --size 96`**: add as a non-blocking job first. It needs node and headless Chrome, so put it in its own job with a timeout.
- **Generated-file checks**: fail if `golden_test --check`, `catalogue_test --check` or the schema generator (step 3) would change a tracked file, using fosfora's `--check` convention.
- **Hygiene**: pin actions by SHA (android.yml uses `@v7` tags). Give each step a timeout of about 3× a warm run.

### R7. Loop export, made deterministic where it can be. Cost M. P2.

- **`LoopSpec.of`** (`LoopRender.kt:86`): when the track's tempo is known, snap `loopMs` to whole bars. `frames = round(bars·4·60/bpm·fps)`, `effectiveBpm = bars·240·fps/frames`; report requested → effective. A JVM test ports `snap_bound_over_bpm_sweep` and `lossless_snap_at_120` (`loop_spec.rs:356-393`).
- **Seam test on the emulator** (`app/src/androidTest/.../export/LoopSeamTest.kt`, SwiftShader is enough): render frames 0, 1, N−1 and N plus three mid pairs through `OffscreenSceneRenderer` with `SyntheticGroove`, then assert MAD(N−1 → 0 after the crossfade) ≤ 1.5 × the largest ordinary step + 1. This is fosfora's wrap-smoothness rule. Geode styles are stateful (trails, fluids, the camera), so bit-exact closure does not apply; the smoothness bound does.
- **Do not build a phase-locked mode** until a style family is a pure function of the uniforms. If one is ever built, gate it the fosfora way: a source lint in CI, a denylist in `check_shaders.py` of `uTime` and trail reads for styles tagged `phase_locked` in `Catalogue.cpp`, and a pixel probe.

### R8. A headless render run for judging. Cost L. P3.

An androidTest version of `--render-scene`: `OffscreenSceneRenderer` scheduled from FileDriver's sections, writing stills, clips, a `run.json` and its warnings. It gives Director and Form rounds a visual artifact. It needs R1-R4 first, and EGL limits equal to the live `VisualizerView`'s (`headless/gpu.rs:43`).

### R9. Small honesty fixes. Cost S. P1.

- Correct the plan's §2 row F3: `lib_sound` names are checked by `SoundFrameLayoutTest`, not `glslcheck`.
- Write in AGENTS.md (or ADR 0006) that the golden vector is a host contract, with a device tolerance of about 2e-4.
- Record which export fps the encoder actually accepted: return `fps` in the render result and show it in Studio's notice. Until step 4 lands, the sound engine's output depends on it.

## 6. Do not copy

- **`#[ignore]` as the home for every pixel gate.** A gate that CI never runs decays. Geode has an emulator job: put seam and offscreen checks there, or label them as release checks in `DEVICE_CHECKS.md` with their numbers.
- **Harmonix audio from YouTube via yt-dlp**, and any dataset audio in the repo or in CI. Geode is offline by design, and the bench stays a dev-only fetch outside the tree.
- **Python in the blocking path for the fixture.** Fosfora needs `uv` and mir_eval in CI. Geode's fixture fits in about 200 lines of C++ with no new toolchain.
- **Personal paths in frozen manifests** (`/mnt/t9/...`), and unpinned `>=` dependencies in bench scripts.
- **Three-hop pins at one absolute tolerance on mixed-scale fields.** Pin whole frames, with per-kind tolerances (R2).
- **A raw-source denylist as the only stateless proof.** If Geode ever needs one, declare state in the catalogue and check the declaration, with the denylist as a backstop.
- **Re-implementing the input fold and the clock in the offline driver** (`analyze/mod.rs:97-102`). In Geode, FileDriver and the live GL-thread feed should call one `FrameClock` and one `SoundEngine::write` path, not parallel copies.
- **Feature-set clippy, Miri, the `cargo-deny` GPL check, four-OS builds**: Rust and desktop problems Geode does not have. Its equivalents are sanitizers (R6), `hostlink` and the NDK build.
