---
status: proposed
---

# An export analyses the whole track offline; live stays causal

On 2026-11-05 the owner settled analysis for Spec 3.0 ([#455](https://github.com/tessie1993/geode-app/issues/455) § Analysis, answers on the session board [#433](https://github.com/tessie1993/geode-app/issues/433)). "Live hears causally, export hears everything offline," and the owner accepts that an export can look different from live. This ADR records that decision. It amends two earlier ones:

- [ADR 0006](0006-fosfora-aligned-engine.md) says export becomes a driver of the live chain, so "live equals export" is proven by a test. That no longer holds for an export's features. The `core/output` driver still owns the frame clock and the pre-roll, but it reads features from the offline timeline, not from the hop chain.
- [#302](https://github.com/tessie1993/geode-app/issues/302)'s line "Export only replays the same live scene faster than real time" no longer holds. An export renders the same Scene from the offline feature timeline.

## Why

- **A live detector can only predict.** Section changes arrive a bar late, drops only once the bass is back, and the first seconds of a track are spent locking. An export has the whole file before its first frame, so a cut, a camera move or a Scene change can land on the real boundary.
- **The frame rate must not change the music.** An export at 24, 30 or 60 fps should differ only in frame sampling (spec story 33). A timeline that is computed once and sampled at any time gives that by construction.
- **The best offline methods are non-causal.** Centred windows, a beat grid fitted over the whole song (Ellis 2007), harmonic/percussive separation, CQT chroma and recurrence-based segmentation all look ahead. Making live detectors do that would break live.

## Decision

1. **Two analysers, written separately.**
   - The live one is `core/sound` and later ADR 0006's `core/sound/hop`. It stays causal for every input: track, mic, other apps' audio, wallpaper and dream.
   - The offline one lives in `core/sound/offline`. It is new code, not a mode of the live detectors. They share no stages, only primitives such as `Fft`, so neither is bound by the other's constraints. Group D's hop work (#324, #380, #411) does not move it.
2. **One schema.** The offline analyser writes the same `GeodeSoundFrame` fields as live, with the same meanings: the triggers fire on one frame, the counts count, the phases run 0 to 1. Routes, Scenes and the director therefore read an export exactly as they read the screen. The first slice is `offline::sample()` for the rhythm fields.
3. **Methods follow librosa, and the lab holds them to it.**
   - Each offline method is a step-by-step port of the librosa function it names, pinned at librosa 0.11.0.
   - `tools/analysis_lab` writes librosa's output on labelled clips as goldens, and `core/tests/offline_*_test.cpp --goldens` checks the port against them within stated tolerances.
   - Where Geode adds a method librosa lacks (downbeats), the bench judges it against the truth.
4. **Both analysers are scored against labelled clips in CI.**
   - The bench is `.github/workflows/analysis-bench.yml` and `core/tests/analysis_bench.cpp`. It scores beat, downbeat and section F-measure, key and tempo with mir_eval's conventions.
   - It fails on any regression against `core/tests/analysis/bench_baseline.tsv`.
   - The clips are synthesized from a score, so their answers are exact and they are redistributable. Each one is named in `tools/analysis_lab/manifest.tsv`.
5. **The feature timeline is analysed once and cached.** It is the offline analyser's per-frame feature values for the whole track.
   - A render builds it as its first step and shows "Analysing".
   - It is cached on disk, keyed by the track file's identity plus the analyser version. Any version change invalidates it.
   - The library never reads it.
   - This part is #467's.
6. **Rendering at any frame rate.** Sampling the timeline is a pure function of song time (`sample(grid, seconds, dt, frame)`). Each trigger fires on the one frame whose span holds it, so every frame rate sees every beat once.

## Slices

| Ticket | Offline slice | Golden and bench |
|---|---|---|
| #463 | Onset strength, tempo, dynamic-programming beats (`librosa.beat.beat_track`), downbeats: `OfflineBeats` | onset envelope, tempo and beats against librosa; beat, downbeat and tempo scores |
| #464 | Centred-window bands, levels and onsets (whitened), HPSS, CQT chroma and key | each against librosa; key score |
| #465 | Recurrence-based sections, build-ups and drops, on the real boundary | section and drop F-measure |

## Consequences

- An export can differ from what the performer saw live. The owner accepts this. A take (a screen capture) stays the way to keep exactly what was on screen.
- ADR 0006's `core/output` driver reads features from the timeline, not from the hop chain. Its frame clock, pre-roll and sinks are unchanged.
- The offline analysis costs time before the first export frame. Two pieces dominate:
  - The tempogram is a direct autocorrelation, about 0.1 s of CPU per minute of audio on a desktop core. An FFT version is the first optimisation if a device check finds it slow.
  - The mel spectrogram costs about the same.
  - The cache makes repeat renders and batches skip it.
- Downbeats assume 4/4 with one phase for the whole track, as live's do. A meter or phase change mid-song is future work for the bench to drive.
- The bench is a host contract. Synthetic clips and pinned libraries make it deterministic on the CI runner. Device builds contract floating point differently (report 03, §3), which is one more reason exports read a cached timeline rather than re-deriving features.
