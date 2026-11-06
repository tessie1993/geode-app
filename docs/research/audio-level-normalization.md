# Audio level normalization: ambient must not look like a fat track

> Research note, no engine change. Question: how should a real-time visualizer normalize levels so the
> visuals show the music's actual energy (sparse ambient vs dense, percussive) without a per-song adaptive
> bias? Owner's words: "When I play an ambient track with very little beat, visuals still behave as if a
> fat track is playing." and "Music needs to be real time. No bias."
>
> Evidence tags: **[V]** verified against the source named; **[M]** from memory or a secondary summary,
> not opened at the owner source, treat as unverified.

## Summary

- The cause is by design. Geode's core levels divide each band by that band's own ~4 s running mean
  (`RelativeLevel`), so "1.0" always means "typical for this song right now". A near-silent ambient pad
  and a dense mastered track both sit at ~1.0 on average, and the visuals that read `volAtt`, `bassAtt`,
  `bass`, `mid`, `treble`, `level` and the slice values inherit that. MilkDrop does exactly the same, on
  purpose, so presets stay portable across songs and volumes [V].
- The same ratio is the right tool for "what is changing now" (a hit, a swell) and the wrong one for
  "how much is going on". Those need an absolute or a long-term reference that is not the song's own
  recent mean.
- Geode's playback tap hears the file before ReplayGain and the DSP chain [V], so absolute level survives
  on the playback path. That is an asset: loudness there is not squashed by streaming normalization.
- Loudness alone cannot separate ambient from EDM once material is loudness-normalized (both land near
  the same LUFS) [V for the targets, M for the inference]. Texture features carry the difference: onset
  density, spectral flux, percussiveness, beat confidence. Geode already computes all of them.
- Recommendation: option C, a hybrid. Absolute, fixed-reference loudness for "how much", texture
  features for "how busy", relative ratios kept only for transient shape. No per-song learning, no memory
  longer than a few seconds except an optional minutes-long input gain stage for the mic path.

## Geode today (file:line, branch `origin/main`)

Describes `core/sound` as read; line numbers are for the commit this branch was cut from.

| Piece | Where | What it does |
|---|---|---|
| `RelativeLevel` | `core/sound/Stages.hpp:42-79` | Fast attack envelope `att_` (rise 0.2/frame, fall 0.5/frame, `:79-80`) over a running `mean_` with `kMeanSteady` 0.992/frame (`:81`, ~4.15 s; `kMeanWarm` 0.9 for the first moments). `ratio_ = inst / mean_`, `attRatio_ = att_ / mean_`, both through `softLimit` (`:31-32`, linear to 2, tanh bend toward 4). The header comment says it outright: "1.0 means typical for this song right now whatever the volume or mastering" (`:35-36`). |
| Band ratios | `core/sound/SoundEngine.cpp:301-318` | `bass/mid/treble = band.ratio()`, `*Att = band.attRatio()`, `vol` and `volAtt` are the mean of the three (`:315`). Same shape as projectM (below). |
| TouchDesigner-style layer | `SoundEngine.cpp:320-338` | Slices `low/middle/high` run through a `RelativeLevel` with `bandFloor = kBandFloorRel * levelPeak` (`:328`, `kBandFloorRel` 1e-3 at `:23`), then Lag, then `reactivity * gain * (0.5 * shaped - threshold) + offset`. The halving puts "typical" at 0.5. `presence` is `attRatio` followed over 3 s (`:333-335`). |
| `level` | `SoundEngine.cpp:343-345` | `levelPeak = max(rms, levelPeak * exp(-dt ln2 / 3 s))`, `level = rms / levelPeak`. A 3 s half-life peak follower: any track spans 0..1 within seconds, so a quiet track is stretched to full scale. `levelPeak` also scales the band floor (`:328`, `:541`) and the flux knee (`:471`, `knee = 300 / levelPeak`). |
| `loudness` | `SoundEngine.cpp:346`, `SoundFrame.h:78` | `clamp01(1 + 20 log10(rms) / 60)`: **absolute** dBFS mapping, -60 dB = 0, 0 dB = 1. This is a fixed reference already in the frame. It is RMS, not K-weighted LUFS. |
| `energyTrend`, `intensity` | `SoundEngine.cpp:493-501` | Short (0.4 s) over long (8 s) smoothed squared RMS; `intensity` integrates `max(0, trend - 1)` at 0.15/s and drains at 0.03/s. Relative by construction: "louder than its own average". |
| `flux` | `SoundEngine.cpp:471-492` | Log-compressed positive spectral difference, then `flux.step(...)`, i.e. also divided by its own mean. |
| Drum triggers | `SoundEngine.cpp:525-569` | Kick/snare/hat from band "punch" against the band's own recent past (150 ms), floor `max(kBandFloorRel * levelPeak, kLeakage * total)` (`:541`); the TouchDesigner-style `Trigger` (`Stages.hpp:93`) uses hysteresis plus refractory. Thresholds are user tuning `drum[3]` (defaults 0.6/0.5/0.5, `SoundFrame.h:194`) and `onsetThreshold` (default 2, "relative", `:201`). |
| Beat confidence | `core/sound/Rhythm.cpp:137-176`, `SoundFrame.h:135` | `beatConf` 0..1 from the autocorrelation peak (`acConf_`, `Rhythm.cpp:176`); locked above ~0.35. Below that, beats fall back to the hit clock. |
| Emotion energy | `core/sound/Emotion.cpp:174-197` | `energy = 0.2 * print_.energy * conf + 0.3 * loud + 0.2 * tempo + 0.2 * busy + 0.1 * bright + 0.15 * buildUp`, followed at 1.5 s (`kEnergyTau`, `:34`). `loud` is **absolute** (`(loudness - 0.45) / 0.45`, "about -33 dBFS .. -6 dBFS", `:180`), `busy` = onset rate over ~4 s / 6 per second (`:177`, `:182`). So the Director's feeling already uses the right kind of inputs; the problem is mostly elsewhere. |
| Input tuning | `SoundFrame.h:189-207`, `engine/audio-core/.../SoundTuning.kt` | `gain[3]`, `threshold[3]`, `offset[3]`, `attack/release`, `reactivity`, `silenceDb` (-60) etc. Global trims, applied after the relative ratio, not an input gain stage. |

Consumers in `core/viz` that inherit the relative scale (grep-verified lines):

- `MotionField.cpp:66-70`: `energyRatio = clamp(volAtt, 0, 2)`, slewed into `energyRel`; drives orbit radius
  (`:133`), flow rate (`:159`), speed (`:197`), particle size/density/trail (`:204-206`). This is where an
  ambient track gets "fat track" motion, because `volAtt` averages 1.0.
- `CameraRig.cpp:150-152` scales its amount by `intensity^1.5`; `:208` the FOV breath by `bassAtt - 1`.
- `EndlessZoom.hpp:84`: dive push from `bassAtt` (clamped 0..4).
- `NoteResponse.cpp:203, 208`: pulse from `bassAtt - 1`; "flow" knob from `intensity`, `level`, `energyTrend`.
- `Director.cpp:54, 69` and `Reactions`/`ReactionDna`: routes whose sources are `ReactSource`s over the
  sound frame (`Intensity`, `BassAtt`, ...). Style tuning (every route amount, every shader knob) was set
  against ratios centred on 1.0.

Input sources (`docs/AUDIO_CHAIN.md`, `AGENTS.md`): playback tap, mic, other-app capture all write through
one `PcmSink` into `SampleRing`. The playback tap is step 1 in the Media3 chain; ReplayGain is in step 4,
`NativeDspProcessor` (`docs/AUDIO_CHAIN.md:25-28`), and `ReplayGain.kt` drives it via `audioFx.setGainDb`
(`PlaybackEngine.kt:118`). So the engine hears the original file level, before ReplayGain, volume, EQ and
crossfade. The mic and capture paths have unknown, device-dependent gain. projectM-hosted MilkDrop presets
receive raw PCM and compute their own `bass/mid/treb` (relative) regardless of Geode's frame.

## What others do

| Tool | Level handling | Auto-gain? | Source |
|---|---|---|---|
| MilkDrop / projectM | `bass = band_sum / longAvg`, `bass_att = shortAvg / longAvg`; short avg rates 0.2 rise / 0.5 fall, long avg 0.992 (0.9 first 50 frames), rates defined at 30 fps and rescaled by frame time; `vol = mean(bass, mid, treb)`; `longAvg < 0.001` returns 1.0. Bands are thirds of the first half of the spectrum by equal bin count. Docs: "1 is normal; below ~0.7 is quiet; above ~1.3 is loud". Relative on purpose: presets are authored against 1.0 so they work on any song and any volume. | Yes, intrinsic (ratio to a ~4 s mean); no absolute reference | projectM `Audio/Loudness.cpp`, `Audio/PCM.cpp`, Context7 `/projectm-visualizer/projectm`; projectM wiki Preset Authoring Guide [V] |
| Butterchurn (WebGL MilkDrop) | Same equations, bands fixed in Hz (bass 20-320, mid 320-2800, treb 2800-11025); FPS clamped 15..144 | Same | jberg/butterchurn `src/audio/audioLevels.js`, Context7 `/jberg/butterchurn` [V] |
| TouchDesigner | Audio Spectrum CHOP emits raw magnitude (visualization mode only re-weights the spectrum); Analyze CHOP gives single-number RMS/average/max/peak count over a channel; smoothing is explicit via Lag/Filter CHOPs; range fitting is done by hand with Math CHOP "range" or a Limit. No built-in auto-gain in these docs. | No (user normalizes) | derivative.ca Audio_Spectrum_CHOP, Analyze_CHOP, Filter_CHOP via Context7 `/websites/derivative_ca_userguide` [V]; "no auto-gain" = absence in these pages, [M] beyond them |
| Resolume | Wire FFT gives "the amplitude (loudness) of a set frequency band"; page does not describe gain or auto-gain | Not documented there | resolume.com/support/en/wire-fft [V that it is silent]; the audio-analysis page returned only a link list |
| Magic Music Visuals | Per-input gain set manually in the input selector; no auto-normalize found in forum threads | No (manual gain) | magicmusicvisuals.com forum threads via search; User's Guide returned 403, **unverified** |
| audioMotion-analyzer | Absolute dB window: `minDecibels` -85, `maxDecibels` -25 default, `setSensitivity()`; optional `linearAmplitude`/`linearBoost`; optional A/B/C/D weighting. No adaptive gain | No: fixed window, user adjusts | audiomotion-analyzer README via Context7 `/hvianna/audiomotion-analyzer` [V] |
| Meyda | Features are raw: `rms` ≥ 0, `spectralFlux` "starts at 0, no upper range as it depends on the input", `loudness` Bark-band energy^0.23 | No | meyda docs via Context7 `/meyda/meyda` [V] |
| librosa | `onset_strength` = mean over bands of half-wave-rectified spectral difference on a dB (log) mel spectrogram, optional max-filtered reference (SuperFlux); `onset_detect` peak-picks that envelope and normalizes it by default (`normalize=True`) | Peak-pick uses a local adaptive threshold | librosa `onset.py` via Context7 `/librosa/librosa` [V] |
| Winamp AVS, Kodi, Plane9 | Not researched | n/a | **unverified, not looked at** |
| Ableton/Max envelope followers | Not researched | n/a | **unverified** |

Context7 had good coverage for projectM, Butterchurn, TouchDesigner, Meyda, librosa, audioMotion and
Essentia (`/mtg/essentia`, `/websites/essentia_upf_edu`, resolved but not queried); only the Resolume FFGL
plugin SDK (`/resolume/ffgl`) for Resolume, nothing for Magic Music Visuals, AVS, Kodi, Plane9. Those
fell back to the web pages cited above.

Pattern: the MilkDrop family normalizes relatively and silently; the analysis-toolkit family
(TouchDesigner, Meyda, audioMotion) leaves levels absolute and hands the user a gain/range control. No
tool in the verified set learns the song's character over minutes; the only adaptivity is the ~4 s mean.

## Loudness standards and their consequence

- ITU-R BS.1770 defines K-weighted loudness (LUFS); EBU R 128 sets: momentary 400 ms, short-term 3 s,
  integrated (whole programme), integrated target -23 LUFS, true peak ≤ -1 dBTP; integrated uses an
  absolute gate at -70 LUFS and a relative gate 10 LU below; momentary and short-term are ungated [V,
  Wikipedia EBU R 128 summary, not the EBU PDF].
- Spotify normalizes to -14 LUFS (BS.1770) by default, Loud -11, Quiet -19; applied at playback, files
  unchanged; an album is normalized as a whole so quiet tracks stay quiet relative to louder ones on the
  same album, shuffled tracks individually [V, support.spotify.com loudness normalization]. YouTube
  (-14) and Apple Sound Check (-16) targets [M, not fetched]. ReplayGain 2.0: -18 LUFS reference, track or
  album gain, BS.1770-based [M, the wiki fetch returned 403].
- Consequence [inference, M]: if the played signal is per-track loudness-normalized, an ambient
  track and an EDM track end at similar integrated LUFS. Their difference is then **dynamic range and
  texture**, not level: the ambient track has low crest factor over time and almost no onsets; the EDM
  track has dense onsets and high short-term loudness variance. Two things follow for Geode:
  1. On the playback path Geode taps before ReplayGain [V], so it hears the original masters and absolute
     level is still informative (ambient masters are typically far below -14 LUFS [M]); an absolute loudness
     reference is therefore workable for files.
  2. Any other path that is already normalized (another app's capture after its own normalization, a
     streaming app with "normalize" on) loses absolute level, and then **only texture features separate
     the cases**. The design must not depend on level alone.
- Geode's `loudness` is RMS in dBFS, not K-weighted LUFS. The standards describe the better quantity to
  use for "how loud", with a 3 s short-term window that matches human perception of a section, but an
  RMS-based fixed mapping is a defensible cheaper proxy; LUFS is already implemented natively for export
  (`core/audio/dsp/Loudness`, BS.1770 meter per `AGENTS.md`) and could be reused. [feasibility M]

## What predicts perceived energy

Russell's circumplex puts affect on valence × arousal; "energy" here is arousal [M, Russell 1980].

| Evidence | Finding | Tag |
|---|---|---|
| Panda & Paiva, SMC 2011 and Panda et al., CMMR 2013, on Yang et al.'s 189-clip arousal/valence set | Regression on PsySound/MIR Toolbox/Marsyas features: arousal R² ≈ 63% (valence ≈ 35%); adding melodic features reaches 67.4% / 40.6%. Arousal is far easier than valence | V (via the Coimbra repository abstracts) |
| Yang et al., 2008, "A regression approach to music emotion recognition" | Arousal 58.3% variance explained, valence 28.1% | M (secondary citation in a 2021 review) |
| Eerola, Lartillot, Toiviainen 2011/2013 (film soundtracks, MIRToolbox) | Energy/arousal predicted by timbre, rhythm, register, articulation; loudness, tempo, onset rate and brightness named as cues; no R² obtained here | partly M. Geode's own comment cites "Eerola 2013, Juslin 2010" at `Emotion.cpp:186` |
| Schmidt & Kim; Aljanaki, Yang, Soleymani (DEAM, 2017) | Dynamic arousal annotation datasets; arousal consistently more predictable than valence; loudness, spectral flux/rhythm features are the stronger arousal predictors | M, not opened |
| Bello et al. 2005 tutorial | Onset detection by spectral flux / energy change; threshold is a function (local mean or median) of the detection function plus an offset, because the detection function's scale changes with signal level, dynamics and mix; fixed global thresholds fail across material | partial V (search result summarising the lecture deck); formulas not verified in the paper |

Reading across these: the strongest arousal predictors are loudness, onset/event rate, tempo, spectral
brightness and timbral roughness/flux, with rhythm-percussive content next. Geode's `Emotion` already
mixes these in nearly this ratio. The same features are the ones that distinguish ambient from dense
music **after** loudness normalization: onset rate (ambient: ~0 to a few per minute; dense: many per
second), flux, a percussive-vs-harmonic energy split (HPSS ratio [M, Fitzgerald 2010]), and
`beatConf`. Why adaptive thresholds exist in onset detection: the detection function's amplitude depends
on the mix, so the threshold has to follow it; that is a **peak-picking** reason, not a reason to rescale
the energy the visuals show. Geode's `Trigger`/`onsetThreshold` already use relative thresholds, which is
fine for *detecting* events; the issue is that the *magnitude* handed to visuals (`volAtt` etc.) uses the
same relative scale.

## Input gain for mic and capture

Sources are thin here and mostly [M]; verified evidence is the toolkit pattern above.

- Audio toolkits leave gain to the user (audioMotion `setSensitivity`, Magic per-input gain, TouchDesigner
  range math) [V/partial V].
- Common engineering practice [M, no single owner source cited]: a one-time calibration against a known
  signal, or a slow gain stage (time constants of tens of seconds to minutes) that sets input gain so the
  noise floor and peaks sit in a fixed window, with an estimated noise floor subtracted. The AES/ITU line
  of thinking behind BS.1770 is that a *fixed* K-weighted meter is the absolute yardstick; long-window
  integrated loudness (R 128) is for programme-length offline use, not real time.
- For Geode: the file path has a known scale (full-scale PCM). The mic/capture path does not. A slow
  gain stage there normalizes the **device**, not the song, if its time constant is much longer than any
  musical phrase (minutes) and it is driven by a high percentile of long-term loudness, not the mean.

## Options

| | A. Fixed absolute references | B. Slow gain staging only | C. Hybrid (recommended) |
|---|---|---|---|
| Idea | Map RMS/band energy through a fixed dBFS window (audioMotion-style); drop per-song ratios for "amount" | Replace the 4 s mean by a minutes-long gain; keep ratios | Absolute loudness for "how much"; texture (onset density, flux, percussive share, `beatConf`) for "how busy"; relative ratio only for transient shape |
| Ambient vs fat | Fixed window shows ambient low, fat high, provided level is preserved | Over minutes the gain adapts to the song: ambient is re-inflated, the bias returns, just slower | Ambient: low loudness **and** low onset density; both agree. Normalized streams: texture still separates |
| Loudness-normalized playback | Fails: all songs sit near the same level | Same bias as today | Survives: texture features do not depend on level |
| Mic / capture | Needs a calibration or user gain | Natural fit for device gain | Slow stage on the input only; fixed refs downstream |
| projectM/MilkDrop presets | Unaffected (raw PCM) | Unaffected | Unaffected; Geode frame fields change, presets do not |
| Existing style tuning | Breaks: every route amount assumed 1.0 = typical | Mostly preserved | Preserved if relative fields keep their meaning and new fields are added or composed behind an opt-in gain |
| Host tests | Easy (pure functions) | Moderate (long time constants) | Easy per field; add golden frames |
| Real time / no bias | Yes | Partly (long memory is a bias) | Yes: nothing remembers the song beyond seconds |

Other ideas, briefly: (D) rescale the existing ratios by a long-term "texture score" so a sparse track gets
smaller ratios, a cheap patch that keeps tuning but hides the cause, fine as a first step; (E) per-track
metadata (ReplayGain track loudness/range) as prior, rejected because it is a per-song value the owner
called bias and does not exist for mic.

## Recommendation: option C

Principle: **three questions, three references.** How much (absolute), how busy (texture), what just
changed (relative). Keep the third as is; stop using it for the first two.

Design sketch (all in `core/sound`, host tests in `core/tests`, Kotlin untouched except JNI/tuning pass-through):

1. **`loudnessAbs`** (new field, or reuse `loudness` `SoundFrame.h:78`): fixed mapping, no adaptation. Prefer
   a 3 s K-weighted short-term loudness reusing `core/audio/dsp/Loudness` (BS.1770), mapped -50..-8 LUFS to
   0..1 [calibration targets are a proposal]. Fallback: the existing RMS dBFS mapping. Time constant 3 s
   (R 128 short-term) plus a 400 ms momentary for hit scale.
2. **`density`** (new, 0..1): event density from the existing `onset` events (`Emotion.cpp:174-177` already
   counts them, tau 4 s): `1 - exp(-rate / r0)` with `r0` ≈ 3 onsets/s as a proposal. Use ≥ 3 s memory so
   a single hit does not read as density. Fold in percussiveness: drum-trigger rate and kick/snare strength.
3. **`busy`**: `0.4 density + 0.3 fluxAbs + 0.2 percussiveShare + 0.1 beatConf` where `fluxAbs` is the log
   spectral flux (`SoundEngine.cpp:471`) divided by a **fixed** knee (not `300/levelPeak`) so it keeps its
   absolute scale; weights are proposals, to be fit against labelled clips.
4. **`energyAbs`** = `loudnessAbs` × (0.5 + 0.5 `busy`), smoothed 1.5 s (same tau as `Emotion`). This is the
   "how much is going on" number consumers should scale by.
5. **Consumers**: `MotionField` `energyRatio` (`:66`) becomes `energyRatio = lerp(1, volAtt, k) * energyAbs_scaled`
   so the *magnitude* of orbit/speed/flow/size scales with absolute energy while `volAtt` keeps the pulse
   shape; same for `CameraRig` `intensity` and `NoteResponse` flow (`:208`). Keep `bassAtt` for hits and
   `EndlessZoom` push, but gate their amplitude by `energyAbs` so ambient bass swells do not fire full
   camera breaths.
6. **Retire the bias, not the ratios**: shorten nothing, lengthen nothing; `RelativeLevel` stays at 4 s.
   Remove `levelPeak` from magnitude paths (`level`) and keep it only in trigger floors, where it is a
   detection aid (`SoundEngine.cpp:328`, `:541`, `:471`).
7. **Mic/capture**: a slow input gain stage before the ring or in `SoundInput`: target the 95th percentile
   of 3 s loudness at -14 LUFS-ish, time constant 60-120 s, rise slower than fall (never boost during
   silence, freeze below `silenceDb`). File playback bypasses it. `SoundTuning` gets an optional trim.
8. **Rollout**: ship behind a tuning flag; default per path (file on, capture on after calibration).
   `reactivity` and `gain` keep working on the relative layer.

Risks: re-tuning of every route amount; RMS loudness on bass-heavy EDM vs K-weighted differences;
ambient masters recorded unusually loud; capture of already-normalized streams (texture then dominates,
which is fine).

## Test plan

- `core/tests/sound_test.cpp` (host): synthetic ambient (sustained low-level pad, no onsets) vs dense
  (4-on-the-floor kick + hats, same RMS after loudness-matching to -14 LUFS). Assert `energyAbs(dense) -
  energyAbs(ambient)` ≥ margin at **equal** integrated loudness (the normalized-playback case), and
  unmodified ordering when levels differ.
- Scale invariance of the *relative* fields kept: gain ±12 dB leaves `volAtt` unchanged (regression).
- `sound_golden_test.cpp`: regenerate `golden/sound_frames.tsv` with the new fields; assert the old
  fields are bit-identical.
- Frame-rate drift (30/60/120 fps) on the new fields, as the existing golden report does.
- Mic stage: a 20 dB step in input gain converges within the time constant and does not track song
  changes (ambient → EDM at a constant device gain keeps the ordering); silence freeze.
- Visual checks per `docs/DEVICE_CHECKS.md`: `tools/glslcheck` unaffected; manual A/B on a pad track vs a
  club track with Fluid/Silk/Neon styles.
- Calibration: label ~30 clips (ambient to metal) by ear, fit the `busy` weights and `r0`, report rank
  correlation with the labels; recommend ≥ 0.85 Spearman as a bar [proposal].

## Sources

- projectM `src/libprojectM/Audio/Loudness.cpp`, `PCM.cpp` (Context7 `/projectm-visualizer/projectm`;
  https://github.com/projectm-visualizer/projectm) and the Preset Authoring Guide wiki
- Butterchurn `src/audio/audioLevels.js` (https://github.com/jberg/butterchurn; Context7 `/jberg/butterchurn`)
- TouchDesigner: https://derivative.ca/UserGuide/Audio_Spectrum_CHOP, `Analyze_CHOP`, `Filter_CHOP`
  (Context7 `/websites/derivative_ca_userguide`)
- audioMotion-analyzer README (https://github.com/hvianna/audiomotion-analyzer; Context7 `/hvianna/audiomotion-analyzer`)
- Meyda docs (https://github.com/meyda/meyda; Context7 `/meyda/meyda`)
- librosa `onset.py` (https://github.com/librosa/librosa; Context7 `/librosa/librosa`)
- Resolume Wire FFT: https://resolume.com/support/en/wire-fft
- Magic Music Visuals forum/User's Guide (https://magicmusicvisuals.com/downloads/Magic_UsersGuide.html, not readable)
- EBU R 128 summary: https://en.wikipedia.org/wiki/EBU_R_128 (owner doc: tech.ebu.ch R 128, not fetched);
  ITU-R BS.1770 (not fetched)
- Spotify loudness normalization: https://support.spotify.com/us/artists/article/loudness-normalization/
- ReplayGain 2.0: https://wiki.hydrogenaudio.org/index.php?title=ReplayGain_2.0_specification (403, unverified)
- Panda & Paiva, SMC 2011; Panda et al., CMMR 2013 (Coimbra repository, https://eden.dei.uc.pt/~ruipedro/publications/)
- Yang, Lin, Su, Chen, "A regression approach to music emotion recognition", IEEE TASLP 2008 (secondary)
- Eerola, Lartillot, Toiviainen, timbral/rhythmic predictors of emotion (MIRToolbox) (not opened)
- Aljanaki, Yang, Soleymani, "Developing a benchmark for emotional analysis of music", PLoS ONE 2017 (not opened)
- Bello, Daudet, Abdallah, Duxbury, Davies, Sandler, "A Tutorial on Onset Detection in Music Signals",
  IEEE TSAP 13(5), 2005, DOI 10.1109/TSA.2005.851998 (summary only)
- Russell 1980 circumplex; Fitzgerald 2010 HPSS (not opened)
- Repo: `core/sound/Stages.hpp`, `SoundEngine.cpp`, `Emotion.cpp`, `Rhythm.cpp`, `SoundFrame.h`;
  `core/viz/MotionField.cpp`, `CameraRig.cpp`, `EndlessZoom.hpp`, `NoteResponse.cpp`, `Director.cpp`;
  `docs/AUDIO_CHAIN.md`, `AGENTS.md`
