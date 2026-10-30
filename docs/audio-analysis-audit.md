# Audio analysis audit: hard-coded values on the PCM → feature path

Scope: everything between the PCM tap and the `AudioFeatures` / `GeodeFeatureFrame` the renderer
reads. The question for each constant: is it a property of the signal that should be **measured**,
or a **design** choice (a math constant, a window shape, a range, a memory length) that is fine as a
number? File:line refers to `e67149d`, before any of the fixes.

The fixes land as separate PRs, one per group, and the **Removed** verdicts describe the state after
them:

| Group | Branch |
|---|---|
| FFT size at low sample rates | `claude/analysis-fft-low-rates` |
| Tempo without a preferred BPM | `claude/analysis-tempo-no-prior` |
| Structure events relative to the track's loudness | `claude/analysis-structure-relative` |
| Hop from the window, live clocked by the ring, live = offline | `claude/analysis-hop-clock` |

Each adds its own `core/tests/analysis_*_test.cpp`, which the CI step from this PR builds and runs.

## The path

```
PcmTap (Media3 tee, first in the chain)   NativeTapPump (native player)   AudioCapturePump (mic, other apps)
                         \                         |                         /
                          └──────────────► SampleRing (planar, epochs) ◄────┘
                                                   │
           live: AnalysisEngine ── HopCursor ── readInterleaved ── push/pull ──┐
                                                                              ├─► core/analysis AnalysisSession
  offline: OfflineAnalyzer ── MediaCodec decode ─────────────── push/pull ────┘     (kissfft, LogBands, AdaptiveRange,
                                                                                     SuperFlux, OnsetPeakPicker,
                                                                                     TempoTracker, BeatGrid, BarTracker,
                                                                                     DrumChannels, StructureTracker,
                                                                                     Chromagram, KeyDetector)
                                                   │
                     AudioFeatures → AudioBus / VisualizerRenderer → core/viz Reactions (lib_react)
```

## How other real-time visualisers and trackers do it

| System | Window / hop | Clock | Levels | Tempo |
|---|---|---|---|---|
| projectM / MilkDrop (`third_party/projectm`, `Audio/PCM.cpp`, `Loudness.cpp`) | the newest 576 samples per render frame | render frame, rates scaled by the measured frame time | each band over its own long-term average (1.0 = average) | none |
| aubio (onset, tempo) | 1024 or 2048 window, hop = window/4 (onset) or /2 (tempo), in samples | the stream | spectral flux with an adaptive median threshold | autocorrelation of the onset envelope, Rayleigh weighting around ~120 BPM |
| BTrack | 1024 / 512 | the stream | adaptive threshold | comb filterbank, tempo weighted toward ~120 BPM |
| madmom online (Böck, author of SuperFlux) | hop 10 ms of samples | the stream | log-filtered spectral flux | comb / DBN over a 55–215 BPM range, no preferred tempo inside it |

What this app now takes from them:
- **The hop is in samples, a quarter of the window** (aubio's onset default, the madmom shape), so
  the frame rate is `sampleRate / (fft / 4)`: 86.1 Hz on the 44.1 kHz family, 93.75 Hz on the
  48 kHz family. Nothing picks 60 or 62.5 Hz any more.
- **Live analysis is clocked by the audio in the ring** (the stream), not a timer. projectM's
  "newest window per tick" is fine for a picture with no tempo tracker. For one that counts beats
  it re-analyses stale windows and counts ticks instead of samples.
- **Levels are relative to the music's own running statistics** (projectM's idea), which
  `AdaptiveRange` and `AdaptiveWhitening` already did for the bands. The structure tracker now
  does too.
- **No tempo prior.** aubio, BTrack and librosa (`start_bpm=120`) bias toward 120 BPM to settle
  octave ambiguity. madmom shows a range with no preference inside it works. Here the octave is
  read from the comb bank itself: a pulse train of period T rings T, 2T, 3T equally and T/2 at
  about a quarter of that. So the fastest resonator scoring within `kHarmonicTie` of the best is
  the pulse.
- **kissfft** stays as the FFT (`Spectrum.cpp`, real FFT). Bands are defined in Hz
  (`LogBands`), not by bin index the way projectM's thirds are.

## Inventory

**Removed** = it now comes from the stream or from running statistics. **Kept** = a design or
math constant, with why. **Follow-up** = real, but out of this change's scope.

### Rate, FFT, hop, clock

| Where | What it did | Verdict | Now derived from |
|---|---|---|---|
| `AnalysisEngine.kt:283,294-295` | 16 ms tick, `HOP_RATE_HZ = 62.5`, `DT_SECONDS = 0.016` | **Removed** | hop = FFT/4 samples, rate = stream rate / hop; the loop wakes once per hop of audio |
| `OfflineAnalyzer.kt:319` | `OFFLINE_HOP_RATE_HZ = 60` | **Removed** | same hop as live (`ReactiveAnalyzer.hopSamples`) |
| `ReactiveAnalyzer.kt:8-10`, `ReactiveAnalyzer.hpp:24` | defaults fft 2048, 48000 Hz, 62.5 Hz | **Removed** | no defaults; rate from the stream, FFT from `AnalysisFft.sizeFor` |
| `AnalysisEngine.kt:289` | analyse at 48 kHz until the tap reports | **Removed** | nothing is analysed until a rate arrives |
| `Chromagram.hpp:17` | default hop rate 60 | **Removed** | the session's hop rate |
| `AnalysisSession.cpp:19-22` | `hopFor(rate, hopRateHz)` clamped to the FFT | **Removed** | `fft / kHopDivisor`, never clamps |
| `AnalysisFft.kt:28` | rates below 48 kHz keep 2048 (22.05 kHz: doubled window, halved frame rate) | **Removed** | smallest power of two with the reference bin width (1024 at 22.05 kHz) |
| `AnalysisEngine.kt:157-166` + `MidSideWindow` | re-analysed the newest window every tick, so stale while paused and between deliveries | **Removed** | `HopCursor`: every sample fed once, in order; silence when the source stops |
| `AnalysisEngine.kt:292` | `PULSE_HOLD_HOPS = 3` (48 ms only at 62.5 Hz) | **Removed** | `PULSE_HOLD_SECONDS × hopRate` |
| `StructureTracker.hpp:12` | per-hop novelty decay `0.9997` | **Removed** | `kNoveltyPeakSeconds = 55`, the same memory at any hop rate |
| `OfflineAnalyzer.kt:211` | frames stamped at the window start (export led the screen by one window) | **Removed** | stamped at the window end, which is when live sees the same frame |
| `AnalysisSession.cpp:162-175` | live waveform block-averaged, offline decimated | **Removed** | both block-averaged (one path) |
| `AnalysisEngine.kt:28`, `CaptureController.kt:150` (44100 fallback when mic stops before any tap rate) | **Kept** | the next tap format overwrites it; nothing is in the ring for that source |
| `MicCapture.kt:126,129`, `PlaybackCapture.kt:82,118` (48k/44.1k/22.05k tries) | **Kept** | the device's own rate is tried first; these are fallbacks, and the opened rate is reported |
| `PcmTap.kt:104` `STAGING_FRAMES = 4096`, `NativeTapPump.kt:63` 2048, `AudioCapturePump.kt:122` 1024 | **Kept** | copy chunk sizes, not signal properties |
| `PlaybackEngine.kt:35` ring 65536 frames | **Kept** | capacity (1.4 s at 48 kHz) |
| `VisualizerWallpaperService.kt:283`, `VisualizerDreamService.kt:145` `FEED_INTERVAL_MS = 16` | **Kept** | a display-side poll of `AudioBus` with the real `dt` measured; analysis pulses are held 48 ms, so a 16 ms poll catches each one |
| `BeatTuning.kt:23` `REFERENCE_HOP_HZ = 62.5` | **Removed** as a hop rate | renamed `SETTING_STEP_SECONDS = 0.016`: the unit the saved attack/decay settings are stored in, converted to seconds once |
| `AudioPresentationClock` / `SinkClockDriver` built but unread; the tap leads the speaker by the sink buffer (~250 ms on ExoPlayer) | **Follow-up** | read the window at `inputPositionAt(player position)`, i.e. the sink's real timestamp; needs on-device sync checks |
| playback speed ≠ 1 | **Follow-up** | BPM is reported in content tempo (the tap is before Sonic); the heard tempo needs the clock's `speed` |

### Levels, normalisation, smoothing

| Where | What | Verdict | Why |
|---|---|---|---|
| `StructureTracker.hpp:18,23,25` drop > RMS 0.75, arrival quiet < 0.15 / back > 0.4, on **absolute RMS** | louder than a full-scale sine; drops and arrivals almost never fired on mastered music | **Removed** | fed `macroEnergy` (RMS over its 20 s decaying peak), so the thresholds are fractions of the track's own loudness; 20 s chosen with you |
| `ReactiveAnalyzer.cpp:48-57` digital silence skipped the structure tracker | an arrival after a real silent break could never count its quiet | **Removed** | silence now steps the tracker and decays the loudness peak |
| `AdaptiveRange.hpp:10-12` floor/ceiling taus, 15 dB min span, 1.5 s warm-up | **Kept** | already adaptive per band; these are its memory lengths |
| `AdaptiveRange::kSilenceDb = -120`, `ReactiveAnalyzer::kSilenceRms = 1e-5` (-100 dBFS) | **Kept** | the digital-silence gate, below any real noise floor |
| `AdaptiveWhitening` 2 s peak decay, 1e-8 power floor | **Kept** | memory length, and a numeric floor at -80 dB |
| `ReactiveAnalyzer.hpp:76` macro peak 20 s | **Kept** | the loudness reference's memory (you chose 20 s) |
| `LiveInputProfile` gains (Room, Voice…) | **Kept** | user-selected source profiles, not measurements |
| attack/decay defaults 0.6 / 0.12 | **Kept** | user settings |
| `Reactions.cpp` decays, `kPeakDecaySeconds` normalisation | **Kept** | already adaptive; scene code (owned by the visual-engine work) |

### Bands and spectral edges (Hz)

| Where | What | Verdict |
|---|---|---|
| `LogBands.hpp` 30 Hz – 16 kHz, 3 dB/oct pink tilt at 1 kHz | **Kept**: the audible range and the pink-noise reference; in Hz, so every rate agrees |
| `DrumChannels.hpp:30` kick 30–120, snare 120–900, hat 4–16 kHz | **Kept**: band definitions in Hz |
| `Chromagram` 200 Hz – 5 kHz, `KeyDetector` 60 Hz – 5 kHz, A = 440 Hz | **Kept**: pitch definitions |
| bass/mid/treble at band 1/8 and 1/2 | **Kept**: log bands are Hz-defined, so these are fixed Hz splits |
| `KeyDetector` Krumhansl–Kessler profiles | **Kept**: published key profiles |

### Tempo, beat, bar, structure

| Where | What | Verdict | Now |
|---|---|---|---|
| `TempoTracker.hpp:9` `preferredBpm = 120`, `preferenceOctaves = 1.1` | a Gaussian pull toward 120 BPM | **Removed** | no preference; octave from the comb evidence (`kHarmonicTie = 0.85`) |
| `TempoTracker.hpp:8` 60–200 BPM, 64 log-spaced resonators | **Changed** | 40–240 BPM, every whole-hop period (resolution no longer thins at slow tempi) |
| comb input with its DC level | **Removed** | input minus its running mean, so only periodic variation scores |
| `TempoTracker` 4 s half-life, 2 s energy average | **Kept** | memory lengths |
| `BeatGrid` lock 0.4, tolerance 0.12, correction 0.25 | **Kept** | phase-locking design |
| `BarTracker::kBeatsPerBar = 4` | **Follow-up** | meter detection is a feature; 3/4 and 6/8 currently read as 4/4 |
| `StructureTracker` 5 s warm-up, 8 s section refractory, 4 s drop refractory | **Kept** | event pacing; events stay gated during warm-up (your choice) |
| `OnsetPeakPicker` 1.5 s median window, σ sensitivity, refractory | **Kept** | adaptive threshold already; sensitivity is the user's |
| `Reactions.cpp:36-37` Tempo source maps 60–180 BPM to 0..1 | **Follow-up** | scene code, owned by the visual-engine work; should match the tracker's 40–240 |
| `FeatureTimeline.kt:17` default `hopRateHz = 60` | **Removed** | required; the analyser's measured rate |

### Test data on the live path

None. No synthesised beats, placeholder BPMs or fixed features reach `AudioFeatures`.

## Tests

- `core/tests/analysis_test.cpp` (CI "Native host tests"): click trains at 72–174 BPM at 22.05,
  44.1, 48 and 96 kHz must read within 2% (not folded, not pulled to 120). The hop must be a
  quarter of the window. An arrival after a break must fire at -6 and -34 dBFS. A 1 kHz tone must
  land in the same band at every rate, and loud and quiet copies must normalise alike. Frames must
  be bit-identical whether the PCM arrives in decoder chunks, hops or odd sizes (live = offline).
  Silence must read as silence.
- `HopCursorTest`, `SampleRingTest`, `AnalysisFftTest` (JVM).
