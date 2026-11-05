# core/sound/hop (skeleton, empty)

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md). Nothing here yet.

**Owns:** `HopAnalyzer`, every detector run in one explicit order on a fixed hop of samples, and the thread that drives it: one per audio source, not one per renderer. The same `HopAnalyzer` is driven by a file for export and for tests, so live equals offline by construction.

**Contract**
- In: samples from `core/audio/ingest`.
- Out: one timestamped `GeodeSoundFrame` per hop (timestamp = sample position), pushed into a small drop-oldest frame ring; one-hop pulses (beat, drop, section) kept as counters so a slow reader never loses one.
- Clock: the hop count. Nothing reads wall time or frame dt.
- Order (each stage a class with declared inputs, no shared `out&`): DC strip → filter bank → FFT → levels and spectrum → onsets → drums → rhythm (tempo, beat, bar) → melody → harmony → **pre-normalisation snapshot** → form (sections, build, drop) → emotion → normalise per schema policy → one silence flag.

**Moves in (today, `cf7e88f`)**
- `core/sound/SoundEngine.cpp`: `write()` (`:161-177`) and `step()` (`:200-239`) merge into one `process_hop`; the per-frame half stops depending on dt.
- `core/sound/Rhythm`, `Harmony`, `Form` as stages. Their back-edges (`Form::step` takes `Rhythm&`, `Form.hpp:34`; `harmonyAccent`, `SoundEngine.cpp:786`) become declared outputs one stage reads from the last.
- `core/sound/Emotion`, today owned by `core/viz/Director` (`Director.hpp:113`).
- `core/sound/Silence.hpp`, the engine's digital-silence gate, the tunable silence threshold and starvation (`SoundEngine.cpp:12-17`) become one silence state with named thresholds.
- Worth restoring from release 52 (`db3167f8^:core/analysis/`): `SuperFlux`, `AdaptiveWhitening`, `OnsetPeakPicker` as the onset stage.
- Leaves `core/viz/Renderer` (`sound_`, `soundLock_`, `writeAudio`; `Renderer.cpp:74-81`, `RendererFrame.cpp:47-64`).

**fosfora:** `audio/hop.rs` (`HopAnalyzer::process_hop`, "the one order that is correct"), `audio/mod.rs` (`audio_thread`), `analyze/mod.rs` (the file driver).

**Test that proves it (do this first, before moving anything):** a golden test. A synthetic signal (a fosfora `golden_signal()`-style generator) and a short WAV go through today's engine; the frames are written as JSONL and checked in. Every later step must reproduce them, or change them in the same PR with the reason. A second test feeds the same audio in chunks of 1, 7 and 4096 samples and asserts identical frames.
