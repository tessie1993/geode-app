# core/audio/output

The playback output: Oboe as the device stream under Media3's `DefaultAudioSink`, in place of the `AudioTrack`, for plain PCM. ExoPlayer keeps decoding, the queue, the processor chain (tap, DSP, Sonic), the media position and gapless joins; only the last hop to the device is here. Off by default (Settings › Playback › Output and more), and the platform track takes over whenever this cannot play a config.

Not `core/output` (the export's frame clock and encode plan) and not the removed `core/audio/player` (a whole native player).

**Here now**
- `OutputFifo.{hpp,cpp}` (`geode::audio::output`): the single-producer single-consumer FIFO between the sink's playback thread and the device callback, in `PcmRing`'s style (frame counts that only grow, relaxed-atomic samples, no lock and no allocation on either side). It converts 16-bit to float (x / 32768) on the way in, zero-fills and counts underruns on the way out, applies the volume with a 10 ms ramp, and keeps the position model. No Oboe, so the host test reaches all of it.
- `OboeOutput.{hpp,cpp}`: the Oboe stream (shared, `PerformanceMode::None`, float, `Usage::Media`, `ContentType::Music`, the player's audio session id) whose data callback only calls `OutputFifo::read`. The one place in `core/` that includes Oboe; compiled for Android only, never by a host test.
- The C ABI is `geode_audio_out_*` in `core/api/geode_audio_out_api.cpp`; `app/src/main/cpp/geode_audio_out_jni.cpp` marshals; the shell is `app/.../audio/OboeAudioOutput.kt` (Media3's `AudioOutput`, one native call per method) and the provider in `TapRenderersFactory`.

**Contract**
- In: interleaved 16-bit or float PCM, mono or stereo, 8 to 192 kHz, from Media3's direct buffer (read where it lies). A write never blocks: it takes the whole frames that fit and says how many.
- Out: float frames to the device callback; silence when dry.
- Position: `positionUs` is how much of what was written the listener has heard. From the stream's timestamp (a frame position and the monotonic time it reaches the speaker, run on to now, less the silence the stream was given ahead of the audio), else the frames read less the stream's buffer. It never goes back, never passes the frames read, stands still in pause, restarts at a flush, and after `stop()` ends exactly on the last frame written (also by the clock, should the stream stop reporting), which is what Media3 reads as "ended".
- Length: the sink's buffer size for the output (Media3 asks for about 250 ms of PCM) is shared between the FIFO and the stream's own buffer, so written audio is as far ahead of the speaker as with an `AudioTrack` and `PlaybackSession.SINK_LAG_MS` still holds.
- Route changes: Oboe closes a disconnected stream and calls `onErrorAfterClose` on its own thread; the stream is reopened there on the default device, in the same session, and started if the output was playing. A stream that will not reopen sets `GEODE_AUDIO_OUT_FAILED`; the shell's next write throws a recoverable `WriteException`, Media3 builds a new output, and the provider gives it the platform track.
- Threads: one control thread (Media3's playback thread), the audio callback (reads the FIFO, nothing else: no lock, no allocation, no JNI), Oboe's error thread, and a closing thread per released stream.

**Not done**
- Speed and pitch at the output: Sonic in the processor chain does them, so the output always runs at 1×. A config that asks the output for playback parameters goes to the platform track.
- Offload, tunneling, passthrough, more than two channels, bit-perfect USB: all the platform track's.
- The playout clock (`core/sound/interp/PlayoutClock`, #488) is not fed from here yet. This output has what that clock asks a sink for: `OutputFifo::positionUs` is the sink position in frames of what was written (the clock's `position(sinkUs, nowNs)`, once the shell adds the media time of the first frame written), `play`/`pause`/`flush` are its `playing` and `flush`, and each `write` is a `bufferHandled` anchor when the tap's ring frame for that buffer is passed along. With both in native, the feed could be one call per write with no JNI in between.

**Test that proves it:** `core/tests/output_fifo_test.cpp`: odd chunk sizes lose and repeat nothing; a dry consumer gets silence, counted once per dry spell and not before the first frame or after `stop()`; the position is monotonic, frozen in pause, reset by a flush, equal to the frames heard at 44.1, 48 and 96 kHz across a new device stream, and exact at the end; the volume ramp has no step above its slope; a flush under a running consumer plays no torn or out-of-order frame; a producer and a consumer thread exchange a pattern intact.
