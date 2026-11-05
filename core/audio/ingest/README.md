# core/audio/ingest

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md). The ring and its reader exist (#480). Every source has its own ring: the player tap writes the player's (#481), the mic, other-app capture and wallpaper output mix write theirs from Kotlin's byte buffers (#485, `app/.../audio/AudioBus.kt`). Every live renderer reads its own cursor natively (`core/viz/SoundFeed`, `geode_viz_set_sound_source`); cast's TV audio reads the player's through `geode_ingest_reader_*`. Export moves in #490.

**Owns:** one native PCM ring per audio source (the player tap, the mic, another app's audio, the wallpaper's output mix, a decoded file for export). Each write carries the sample rate, layout and epoch together, so a format change can never pair new samples with an old rate. Converts int16, u8, float, 24- and 32-bit PCM to float here, not in Kotlin. Keeps every channel; readers hear one stereo downmix.

**Files**
- `Ring.hpp/.cpp`: the ring (`Ring`, the `geode_ingest` handle). Single writer, lock-free; format records published before the samples they cover; overrun detection by the write position (`oldestSafe`); the `core/sound/Silence` rule fed by the writes (`resting`: the writer stamps each audible chunk with the monotonic clock, so `resting` is a pure read and every poller gets one answer, #491); what feeds it (`sourceKind`, `GEODE_SOURCE_*`), which `SoundFeed::playerIsSource` reads.
- `Level.hpp`: the ring's UI level (`geode_ingest_level`): the RMS of the newest 512 mono samples, 0 once the ring stops being written. The wallpaper's edge glow reads it; the wallpaper, dream, `SoundSource` and the capture screen read `resting`, so there is no silence or level guess left in Kotlin (#491).
- `Cursor.hpp/.cpp`: one reader's place. `read` gives every sample once, in order, as stereo, one format per block, with `restarted` and `dropped`; `rawMonoTail` is projectM's raw PCM; `skipToNewest` for a stalled live surface.
- `Downmix.hpp`: ITU-R BS.775 weights over AudioFormat channel masks (C and surrounds -3 dB, LFE -10 dB, mono duplicated, stereo bit-exact).
- C ABI: `geode_ingest_*` in `core/api/geode_api.h`; JNI in `app/src/main/cpp/geode_sound_jni.cpp`; `GeodeNative.ingest*`.

**Reader contract (for `core/sound/hop`, Group D)**
- One writer thread per ring. Any number of readers, each with its own `Cursor` on its own thread; the ring outlives them.
- A read returns up to N interleaved stereo frames from one format record: its ring position, rate, source channels and mask, epoch, `restarted` (first block, or the first of a new epoch: reset what was learned) and `dropped` (frames skipped by an overrun or `skipToNewest`).
- `begin_epoch` is a discontinuity (seek, track or source change); `set_format` is the same stream in a new format (speed, pitch, skip-silence) and never restarts readers.
- Rates above 88.2 kHz may be decimated on read; that is the reader's choice.

**Still to move in**
- `engine/audio-core/.../audio/SampleRing.kt`, `SoundCursor.kt`, `SoundInput.kt`, `PcmSink.kt` are deleted (#481, #485); `app/.../audio/PcmRingBuffer.kt` is gone (#486: MilkDrop's raw PCM is the mono mix of what each renderer heard, `core/viz/PcmHandoff`).
- The per-sample int16 -> float loops in `engine/audio-android/.../PcmTap.kt`, `app/.../audio/dsp/NativeDspProcessor.kt` (`AudioCapturePump` and `OutputMixCapture` write bytes since #485).

**fosfora:** `audio/capture.rs` (SPSC ring, no DSP in the callback), `audio/decimate.rs` (rates above 88.2 kHz halved first).

**Test that proves it:** `core/tests/ingest_test.cpp`: random chunk sizes and a mid-stream rate change, every sample once in order with its rate, two readers, a writer thread racing a reader, overrun, epoch vs format, conversion, the 5.1 and 7.1 downmix, mono centred, the raw tail, resting after the hold with two pollers agreeing, the level, the source kind. `core/tests/ingest_level_test.cpp`: the level meter alone.
