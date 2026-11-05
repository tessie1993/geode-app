# core/audio/ingest (skeleton, empty)

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md). Nothing here yet.

**Owns:** one native PCM ring per audio source (the player tap, the mic, another app's audio, a decoded file for export). Each write carries the sample rate and an epoch together, so a format change can never pair new samples with an old rate. Converts int16 to float here, not in Kotlin.

**Contract**
- In: interleaved PCM from the tap's direct `ByteBuffer`, any thread (the player's playback thread, a capture thread).
- Out: a lock-free single-writer ring that `core/sound/hop` reads on its own thread; also the raw tail MilkDrop (projectM) needs, so `pushPcm` goes away.
- Clock: the sample count. No wall time.

**Moves in (today, `cf7e88f`)**
- `engine/audio-core/.../audio/SampleRing.kt`, `SoundCursor.kt`, `SoundInput.kt` (the ring, the each-sample-once cursor, rate as a separate volatile)
- `app/.../audio/PcmRingBuffer.kt` (the second, mono ring that only feeds `pushPcm`)
- the per-sample int16 → float loops in `engine/audio-android/.../PcmTap.kt`, `app/.../audio/AudioCapturePump.kt:77`, `app/.../audio/dsp/NativeDspProcessor.kt:96,107`
- the capture level and silence guesses in `app/.../audio/MicCapture.kt:47-64` and `PlaybackCapture.kt:49` (one silence flag comes from `core/sound/hop` instead)

**fosfora:** `audio/capture.rs` (SPSC ring, no DSP in the callback), `audio/decimate.rs` (rates above 88.2 kHz halved first).

**Test that proves it:** a host test writes PCM in random chunk sizes and at a mid-stream rate change, and checks the reader sees every sample once, in order, each with the rate it was written at.
