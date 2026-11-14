# Music analysis pipeline: the combined design (2026-11-14)

The stage docs hold the sources: [00 architecture](00-architecture.md), [01 ingest and framing](01-ingest-framing.md), [02 spectral front end](02-spectral-front-end.md), [03 rhythm and time](03-rhythm-time.md), [04 pitch and harmony](04-pitch-harmony.md), [05 energy, form and affect](05-energy-form-affect.md), [06 timbre and instruments](06-timbre-instruments.md), [07 features to GL](07-features-to-gl.md).
The earlier survey and the adversarially verified findings are in [../music-analysis.md](../music-analysis.md).
This file only joins them; every claim's source is in the stage doc it names.

## What runs today (00, 07)

- Each renderer owns a `SoundEngine` and runs it on its GL thread before drawing: N surfaces analyse one ring N times; nothing is analysed while no surface draws.
- The analysis is pulled once per GL frame; a stall past the ring's runway drops samples and the unread `skipped` flag splices the gap (01).
- `SoundEngine` is already a hand-scheduled multirate dataflow graph: per-sample nodes, then hop nodes every `hopSamples` (00).
- No size is read from the device; the 10 ms hop, the 40 ms window, A = 440 Hz, 4/4, 8-bar phrases and about a hundred other constants are hand-set (01–05).
- A knob turn resends the whole look across JNI and a mutex; the picture is aligned to render start, not to the frame's display time (07).

## The target

- One dataflow graph per sound source, built at runtime from the device (native rate, burst) and the chosen algorithms; nodes fire on sample counts, never on callbacks (00, 01).
- The audio callback only copies into the ring; a dedicated analysis thread per source runs the graph, with an ADPF session; no thread pinning (01, ../music-analysis.md).
- One shared spectral front end per source: a short right-aligned real STFT for transients, an octave pyramid (one FFT size on half-band-decimated audio) for pitch and chroma, a stable sliding DFT only where per-sample bins are needed; every band scale is a sparse map over those spectra (02).
- Layers by rate: fast (transients, energy) every hop; musical (beat, meter, pitch, chroma, chords, key) at their own rates; slow (form, sections, drop, emotion, instruments) on workers with a latency L fixed at build, so live equals export bit for bit (00).
- Results go to a sample-stamped, lock-free feature history; each surface reads the two frames around the speaker sample for its frame's expected display time and blends each feature in its own space (07).
- Parameters: every param maps to the real line (logit, log, angle); base and every modulator add there, order-free; one map back; the safety clamp once, last (07).
- Knobs: `set_param(slot, value)` into per-slot atomics with a dirty bitset; the full look is pushed only for a preset or look change (07).

## Algebra (the contract every node declares)

- Laws per node: associative, commutative, invertible, idempotent, and whether float arithmetic keeps them exact; the builder uses them to block, parallelise and slide (00).
- Block concatenation is a monoid acting on the state; any split of the stream gives the same features; a gap is its own element (01).
- Windowed sums use group inverses on exact (fixed-point) accumulators; max-like summaries use an O(1) associative sliding window (01, 05).
- Smoothers are affine recurrences: a block folds into one step (Blelloch scan), independent of hop size (00, 05, 07).
- The DFT is the Fourier transform on Z_N; Cooley–Tukey follows a subgroup chain, so 2/3/5 sizes are cheap (02).
- Meter candidates are the subgroups of Z_L; beat phase lives on the circle; autocorrelation is a convolution on Z_N (03).
- Pitch classes are Z_12 (Z_N for any tuning); transposition and inversion form the dihedral group; chroma DFT coefficients give transposition-invariant key, mode and tension (04).
- Statistics are mergeable monoids; gain becomes a shift under log; percentile ranks are invariant to gain (05).
- Pitch shift is a translation on log-frequency; augmentation runs over pitch-shift orbits (06).
- Parameter offsets form an abelian group on the mapped line; hue and angles live on U(1); camera rotation on SO(3) with slerp (07).

## Polymorphism (00)

- Per sample: fused kernels, static dispatch (concepts).
- Per hop: one call through a type-erased node with local storage.
- Per frame: virtual modulators and director.
- Algorithms switched live: `std::variant` over measured backends.
- Memory: one arena sized at build with a null upstream resource; any later allocation fails in a host test.

## Method choices per stage (best evidence found; details and figures in the stage doc)

- Onsets: log-filtered half-wave flux with a causal peak picker (music-analysis.md, verified 3-0); multi-resolution right-aligned STFT front end (02).
- Beat: finish the causal PLP the app approximates; flat tempo range, no prior; particle-filter trackers too heavy for a phone (03).
- Meter and downbeat: DFT over beat accents, candidates are divisors; never 4 or 8 by default (03).
- Drums: fixed-template NMF, about 6 ms delay; full-mix accuracy unmeasured (03).
- Harmonic/percussive: the percussive median adds no latency; the harmonic side only on the slow path (03).
- Tuning reference: causal circular mean of peak deviations (04).
- Lead pitch, multi-pitch, chords, key: candidates and their measured accuracy in 04; causal accuracy for key, chords and polyphonic notes is open.
- Loudness: BS.1770 momentary, short-term, integrated and LRA as mergeable histograms; the −70 LUFS gate replaces hand-set silence gates (05).
- Build-up: Farbood intensity model, its tempo term dropped (05).
- Sections: online Bayesian change points; segment-length prior learned from the track (05).
- Returning sections: causal repetition against the past, key-shift invariant over Z_12 (05).
- Drop: no causal detector with measured accuracy exists; build trend then change point (05, open).
- Instruments and voice: one small tagger trained by us on CC BY data, LiteRT C++ bundled, CPU (06).

## Migration (00; smallest first, the app works after each)

1. Host tests that pin today: chunk-size invariance of `SoundEngine::write`, export equals re-render.
2. `core/graph/Algebra.hpp` (concepts, laws, Fold, Affine) with law tests; no users.
3. Held triggers become a max fold; same bits.
4. Per-sample front end becomes one fused kernel; same bits.
5. Hop stages become kernels in today's order; same bits.
6. A graph planner derives that same schedule from declared rates (test only).
7. Buffers from one arena; test proves no allocation after build.
8. Starved path feeds silence by samples (own golden update).
9. Feature bus and reader views; the renderer reads through the bus.
10. One analysis per ring, shared by every surface on it.
11. Each live source's graph on its own analysis thread.
12. Slow layers at lower rates with fixed latency L, then on workers; ADPF sessions.
13. Alternative algorithms per stage, chosen at build, each with its laws and a host test.
- The stage-specific method changes (02–07) land after step 13, one per PR, each against an annotated test set.

## Owner decisions needed

- Mic over the player: its own analysis, or the mixed sound analysed once.
- Linking permissive libraries in `third_party/` (a NEON FFT, a SIMD library) versus writing them ourselves.
- An annotated test set for tuning (beats, downbeats, onsets, chords, sections, drops) and where it comes from.
- Training our own instrument and voice model on CC BY data.
- Logit easing near a param's range ends instead of a hard clip.
- A slow layer that misses its deadline live: publish late or run inline (export is unaffected).
- `-ffp-contract=off` for identical bits between host tests and phones, at an unmeasured cost.
- Where sound tuning, the mic layer and Record live once analysis is per source (today per renderer).
