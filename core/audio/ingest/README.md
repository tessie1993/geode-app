# core/audio/ingest

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md). The ring is here; the hop thread that should read it is not.

**Owns:** one native PCM ring per audio source (the player tap, the mic, another app's audio, the wallpaper's output-mix capture; later a decoded file for export). Each write carries the sample rate and the source's channel count, so a format change can never pair new samples with an old rate. Converts int16 and unsigned 8-bit to float here, not in Kotlin.

**Here now**
- `PcmRing.{hpp,cpp}` (`geode::audio::ingest`): the lock-free single-writer ring (power-of-two capacity, a runway a slow reader stays behind, an atomic head and epoch), the writers for interleaved float, int16 and raw bytes in an encoding, and `Cursor`, the each-sample-once reader. A take ends where the rate changes, so one take has one rate. Readers hold the ring const and never change it, so a second reader (an output callback) can read the same samples.
- `TrackFeed.{hpp,cpp}` and `AiffReader.{hpp,cpp}`: an export's track audio. The shell decodes (MediaCodec, or the AIFF reader for what MediaExtractor does not read) and pushes each chunk; the feed converts it to float, drops what precedes the range start, and hands the export renderer's engine exactly the frames up to each video frame (`geode_track_feed_*`, `geode_aiff_*`, `geode_viz_hear_track`). The transcoder (`export/AudioTranscoder`) reads AIFF through the same reader.
- `LayerMix.{hpp,cpp}`: a second ring layered over a reader's own (the mic over the player's tap, `geode_pcm_ring_set_layer`). Each renderer mixes at its reader: the layer waits in a short queue (0.1 s to join, 0.25 s at most), is resampled to the block it joins and added, so the player's samples stay the clock; once the player has stopped for 0.3 s the layer is handed out alone. The mic's level is added as it is: on the phone's speaker it hears the track a second time, quieter and a room later.
- `SnapshotStitch.{hpp,cpp}`: a ring marked as a capture's snapshots (`geode_pcm_ring_set_snapshots`; the wallpaper's output mix, ~21 ms of every 50 ms) has each write stretched to the real time since the last one, filled from that snapshot alone by overlap-added grains, so readers get real time with no seam. `snapshot_stitch_test.cpp` proves the clock, the level, and the tempo of click tracks heard that way.
- The C ABI is `geode_pcm_ring_*` and `geode_pcm_cursor_*` in `core/api/geode_ingest_api.cpp`; `geode_viz_feed_ring` (`core/api/geode_viz_api.cpp`) lets a live renderer take from a ring with its own cursor, so no sample crosses JNI on the GL thread.
- Lifetime: a `geode_pcm_ring` handle and every cursor made from it share the ring, which is freed with the last of them. Kotlin's `SampleRing` holds the handle and frees it when it is collected (a `Cleaner`), so a writer thread or a surface that still holds the ring never reaches a freed one; a renderer's feed and Cast's `SoundCursor` keep the ring on their own.

**Contract**
- In: interleaved PCM from the tap's direct `ByteBuffer` (read where it lies), or a capture's `short[]`, `float[]` or `byte[]` (pinned for one copy), any thread, one writer at a time.
- Out: what each reader's cursor has not seen yet, with its rate. Today every renderer's `core/sound` engine reads it on its GL thread; `core/sound/hop` should read it on its own thread. The raw tail MilkDrop needs is the mono mix of what the renderer's engine heard (`core/viz/PcmHandoff`), so `pushPcm` is gone.
- Clock: the sample count. No wall time, except where a snapshot ring's write lands on it (the capture clock, `SnapshotStitch`).

**Moved in**
- `SampleRing.kt`, `SoundCursor.kt` (now handle holders in `engine/audio-core`), `SoundInput.kt` and `PcmSink.kt` (gone: the rate was a separate volatile beside the ring, now it is in each write)
- the per-sample loops of `PcmTap.kt` (16-bit and float from the tap's buffer), `AudioCapturePump.kt` (int16 to float), `OutputMixCapture.kt` (8-bit to float), Cast's float to int16 (`CastStream.kt`, now `geode_pcm_cursor_take_s16`) and the wallpaper's level snapshot (`SoundSource.kt`, now `geode_pcm_ring_heard_at`)
- the per-sample loops and pre-roll cut of `render/offscreen/TrackAudio.kt` and the AIFF header walk and sample conversion of `dev.geode.audio.AiffPcm` (now handles over `TrackFeed` and `AiffReader`)
- the peak scan of `MicCapture.kt` and `PlaybackCapture.kt`: a write returns the block's peak
- ~~`app/.../audio/PcmRingBuffer.kt`~~ gone earlier (#486)

A cursor behind the player's sink takes with a limit (`Cursor::take(limit)`): the frame the speaker
plays now, from the playout clock (`core/sound/interp/SpeakerSync.hpp`), so the picture does not lead the
sound by the sink's buffer.

**Still to move**
- the int16 loops in `app/.../audio/dsp/NativeDspProcessor.kt` (the playback DSP's hand-over, not the ring's)
- the silence and "blocked" timing in `MicCapture.kt` and `PlaybackCapture.kt` (one silence flag should come from `core/sound/hop`)

**fosfora:** `audio/capture.rs` (SPSC ring, no DSP in the callback), `audio/decimate.rs` (rates above 88.2 kHz halved first); Geode halves 88.2 kHz and up inside the sound engine instead, `core/sound/Decimate.hpp` before `SoundEngine::write`'s analysis, so every reader keeps the source's samples (`sound_rate_test`: 96 kHz reads as 48 kHz).

**Tests that prove it:** `core/tests/track_feed_test.cpp` (TrackFeed and AiffReader against the Kotlin they replaced), `core/tests/layer_mix_test.cpp` (the layer: two rates, a silent mic, mic on and off mid-stream, pause and resume, epochs, drift) and `core/tests/pcm_ring_test.cpp`, which writes PCM in random chunk sizes with rate changes mid-stream, on one thread and with a writer thread, and checks the reader sees every sample once, in order, each with the rate it was written at; it also holds the old Kotlin `SoundCursorTest` cases, the encodings and the C ABI.
