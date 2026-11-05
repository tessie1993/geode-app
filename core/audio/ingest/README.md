# core/audio/ingest

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md). The ring and its reader exist (#480); export writes to it through `FileFeed` (#490); the player tap moves in #481 and the capture pumps in #485.

**Owns:** one native PCM ring per audio source (the player tap, the mic, another app's audio, the wallpaper's output mix, a decoded file for export). Each write carries the sample rate, layout and epoch together, so a format change can never pair new samples with an old rate. Converts int16, u8, float, 24- and 32-bit PCM to float here, not in Kotlin. Keeps every channel; readers hear one stereo downmix.

**Files**
- `Ring.hpp/.cpp`: the ring (`Ring`, the `geode_ingest` handle). Single writer, lock-free; format records published before the samples they cover; overrun detection by the write position (`oldestSafe`); the `core/sound/Silence` rule fed by the writes (`resting`).
- `Cursor.hpp/.cpp`: one reader's place. `read` gives every sample once, in order, as stereo, one format per block, with `restarted` and `dropped`; `rawMonoTail` is projectM's raw PCM; `skipToNewest` for a stalled live surface.
- `FileFeed.hpp/.cpp`: the export's file ring (#490, below): a decoded track handed to the export renderer frame-exact; C ABI `geode_file_feed_*`, `geode_viz_feed_file`.
- `Aiff.hpp/.cpp`: the AIFF reader (#653), for the export (MediaExtractor cannot open AIFF): the header walk, COMM's 80-bit float rate, every depth and byte order (AIFC `sowt`) to s16, 24/32-bit truncated with no dither; C ABI `geode_aiff_*`, Kotlin `AiffPcm` only reads the file's bytes. Proved by `core/tests/aiff_test.cpp`.
- `Downmix.hpp`: ITU-R BS.775 weights over AudioFormat channel masks (C and surrounds -3 dB, LFE -10 dB, mono duplicated, stereo bit-exact).
- C ABI: `geode_ingest_*` in `core/api/geode_api.h`; JNI in `app/src/main/cpp/geode_sound_jni.cpp`; `GeodeNative.ingest*`.

**Reader contract (for `core/sound/hop`, Group D)**
- One writer thread per ring. Any number of readers, each with its own `Cursor` on its own thread; the ring outlives them.
- A read returns up to N interleaved stereo frames from one format record: its ring position, rate, source channels and mask, epoch, `restarted` (first block, or the first of a new epoch: reset what was learned) and `dropped` (frames skipped by an overrun or `skipToNewest`).
- `begin_epoch` is a discontinuity (seek, track or source change); `set_format` is the same stream in a new format (speed, pitch, skip-silence) and never restarts readers.
- Rates above 88.2 kHz may be decimated on read; that is the reader's choice.

**Still to move in**
- `engine/audio-core/.../audio/SampleRing.kt`, `SoundCursor.kt`, `SoundInput.kt`, `PcmSink.kt` and `app/.../audio/PcmRingBuffer.kt` are deleted when their callers switch (#481, #485, #486).
- The per-sample int16 -> float loops in `engine/audio-android/.../PcmTap.kt`, `app/.../audio/AudioCapturePump.kt`, `app/.../audio/dsp/NativeDspProcessor.kt`.
- The capture level and silence guesses in `app/.../audio/MicCapture.kt` and `PlaybackCapture.kt` (#491).

**fosfora:** `audio/capture.rs` (SPSC ring, no DSP in the callback), `audio/decimate.rs` (rates above 88.2 kHz halved first).

**Test that proves it:** `core/tests/ingest_test.cpp`: random chunk sizes and a mid-stream rate change, every sample once in order with its rate, two readers, a writer thread racing a reader, overrun, epoch vs format, conversion, the 5.1 and 7.1 downmix, mono centred, the raw tail, resting after the hold.

**Export file ring (`FileFeed`, #490):** the decoded track of an export. `TrackAudio` (Kotlin) only runs MediaExtractor and MediaCodec and hands each output buffer's bytes here; `FileFeed` drops what precedes the range start, writes the rest into its own `Ring`, and reads it back through a `Cursor` so video frame n gets exactly the samples [floor(n · rate / fps), floor((n + 1) · rate / fps)). Proved by `core/tests/file_feed_test.cpp`. The export renderer gets projectM's raw PCM from the same samples through `Renderer::writeAudio` (#486).
