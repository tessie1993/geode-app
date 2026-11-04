# Reference study: fosfora and audioFlux

Two open-source projects were read as guides for Geode's architecture, in November 2026. Nothing was copied. Anything derived from them later gets a `THIRD_PARTY_NOTICES` entry.

| Project | What it is | Licence | Read at |
|---|---|---|---|
| [kevinraymond/fosfora](https://github.com/kevinraymond/fosfora) | Rust/wgpu real-time music visualizer with headless loop and scene export | MIT or Apache-2.0 | default branch, depth 1 |
| [libAudioFlux/audioFlux](https://github.com/libAudioFlux/audioFlux) | C99 audio feature extraction library with Python bindings | MIT, "Copyright (c) 2021--2024 audioFlux development team" | `0c3f55b` (2026-03-06) |

## fosfora: what to take

### Tree

fosfora is one library crate, `fosfora-app`. It has a thin desktop binary, and the same library builds for Android with `--no-default-features`. Its boundaries are Rust modules and cargo features, and only convention holds them in place. Geode's Gradle modules are enforced by the compiler.

```
audio/   capture → hop chain → AudioFrame        (no GPU imports)
bindings/  sources → transforms → targets        (the modulation bus)
params/  ParamDef / ParamStore                   (leaf)
effect/  .pfx effect format, loader, shader preamble
gpu/     frame_prep → frame_graph → compositor → postprocess
preset/ scene/   presets, cues, transitions
headless/  offline render reusing the live per-frame functions
output/  one FrameSink interface for every sink
ui/      reads a state DTO, writes through a request queue
```

### Signal flow

1. **Capture.** The audio callback does no DSP. It pushes samples into a lock-free single-producer ring.
2. **Analysis thread.** It cuts exact 512-sample hops, timed by the sample clock, never the wall clock. One pure function, `process_hop(hop, ts, config)`, runs the whole chain:
   - DC block
   - FFT at three sizes
   - bands, loudness, HPSS
   - YIN pitch, CQT chroma and key
   - SuperFlux onsets, then tempo and beat phase
   - structure: novelty, build-up, drop
   - smoothing
3. **Feature table.** One ordered table (`schema.rs`) gives every feature its name, normalisation, smoothing, stall decay and interpolation policy. The normaliser, smoother and interpolator all read that table. Nothing indexes a feature by number.
4. **Hand-off.** Frames go through a bounded drop-oldest queue, so the newest frame always arrives. One-frame events (beat, downbeat, drop) also bump atomic counters, so a dropped frame can't lose a beat.
5. **Render-side interpolation.** The render thread reads 1.5 hops behind the audio clock, per feature lerp-or-hold. Small phase-locked loops advance the beat and bar phase between hops.
6. **To the GPU.**
   - Uniforms grow only by appending.
   - Waveform, spectrum and mel history go in textures.
   - One function maps features to uniforms for both live and headless rendering.
7. **Modulation as an overlay.**
   - Bindings write *modulated* values that are cleared every frame.
   - The user's base values are never written.
   - Beat-synced LFOs are computed purely from `beat_index + beat_phase`, so they replay identically in export.
8. **Offline.** Headless rendering drives the same hop function from a decoded file, and it calls the same frame-prep and frame-graph functions as live rendering.

### Feature flow

- An effect is a data file with a parameter schema, a pass graph, *rates* (speed parameters the engine turns into running phases, so shaders never compute `time * speed`) and a loop flag. A shared shader preamble is prepended to each one.
- Presets stack up to eight layers. Scenes are cue lists with Cut, Dissolve or ParamMorph transitions.
- A transition is staged: decode off-thread, capture what is on screen, then apply.

### Avoid

- **God objects.** `app.rs` and `main.rs` each run to about 5k lines and own every subsystem.
- **Field-by-field mirroring** of features into uniforms. Generate this from the feature table instead.
- **Two separate modulation systems.**
- **String-keyed maps on the per-frame path.**
- **`version` fields that nothing reads.**
- **Units hidden by normalisation.** BPM is stored divided by 300, which once caused a 300× bug.

## audioFlux: what to take

audioFlux is a catalogue of algorithms, not a dependency. It is whole-signal, float32, and allocates during compute. Every port is rewritten into Geode's own C++ with fixed buffers and Geode's own `Fft`, so live and export stay bit-identical. The library is never linked.

Its layering, which is a good model for an analysis engine:

```
DSP (FFT, DCT, windows, resample)
  → filterbanks (mel, bark, ERB, log, chroma)
    → spectrograms / transforms (STFT, CQT, NSGT, CWT …)
      → features (spectral shape, novelty functions, cepstra)
        → MIR (onset, pitch, HPSS)
```

Its API shape is worth copying: one analyzer object per algorithm with an explicit config, which does all its allocation in its constructor. Underneath sit stateless kernels that can be tested on the host.

### Worth porting into `engine/native/sound`

| Algorithm | Source in audioFlux | Why | Notes |
|---|---|---|---|
| SuperFlux max filter on spectral flux | `src/mir/onset_algorithm.c`, `src/flux_spectral.c` | Fewer false onsets from vibrato and glides | O(bins). Keep Geode's causal peak picking; theirs looks ahead and normalises over the whole track. |
| Rectified complex-domain onset (RCD) | `src/flux_spectral.c` | Catches soft tonal onsets that magnitude flux misses | Needs FFT phase, which `Fft::magnitudes` currently drops. Don't port their phase deviation: it never wraps the phase. |
| YIN pitch | `src/mir/_pitch_yin.c` | Monophonic cross-check for vocals and bass. The trough list is the candidate set for a later pYIN-style tracker. | Use fixed buffers. The real default threshold is 0.1. |
| Hann-window peak frequency correction | `src/dsp/flux_correct.c` | Exact sinusoid frequency from neighbouring bins, which gives better pitch-class assignment for chroma | About 30 lines |
| Spectral shape: spread, skewness, kurtosis, entropy, crest, slope, decrease | `src/flux_spectral.c` | Timbre inputs for Emotion and Form | Trivial loops over the existing magnitudes |
| Mel/ERB scale conversions | `src/filterbank/auditory_filterBank.c` | Only if Form needs an MFCC-style timbre vector | Store weights sparsely |

**Not taken:**
- **CQT and HPSS** are offline. A causal percussiveness estimate is Geode's own design: medians over past frames against medians across bins.
- **NSGT, wavelets, synchrosqueezing, NMF and the phase vocoder** are not needed for real-time visuals.

## How this shapes Geode

These choices are made in [ARCHITECTURE.md](ARCHITECTURE.md) and [SIGNAL_FLOW.md](SIGNAL_FLOW.md):

1. **`SoundFrame` becomes a feature ABI with a policy table,** in the spirit of fosfora's feature table. The reaction-matrix sources, smoothing and the JNI layout are generated from that one table.
2. **The sound engine runs on a fixed hop timed by the sample clock,** with pure analyzers (audioFlux's API shape). Live and export are the same function over different sources.
3. **Modulation never writes base values.** Reactions, waves, envelopes and macros write an overlay that is cleared each frame. There is one modulation graph, not two.
4. **Live and export call one per-frame path.** It is scene → trails → composite, shared by preview, wallpaper, dream and export.
5. **There are no god objects.** `PlayerSession` dissolves into repositories and use cases (ADR 0006).
