# core/sound/interp

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md).

**Here now:** `PlayoutClock` (#349, #482), the presentation map this reads: from anchors the sink's buffers give (media time, ring sample, frames) and the sink's media-time position (`AudioTrack.getTimestamp`, so HAL and Bluetooth latency included; speed and skipped silence already counted), the ring sample the speaker plays at a monotonic time, plus the user A/V offset, never past the newest sample written. A pure read, so every frame rate gets the same sample at the same time. C ABI `geode_playout_*`; proved by `core/tests/playout_test.cpp`. Not yet fed by a sink or read by a renderer: that is #488. The frame interpolation below is still to come (Group D).

**Owns:** how a renderer reads sound. It asks for the frame at a presentation time (a sample position) and gets the two hop frames around it blended by each field's policy from `core/sound/schema`: lerp for levels, hold for clocks and states, latch for one-hop pulses so none is lost or doubled.

**Contract**
- In: the frame ring from `core/sound/hop`; the presentation position (the player's output position minus the sink latency, plus a user A/V offset).
- Out: one `GeodeSoundFrame` per drawn frame, identical in shape to today's.
- Live reads behind the newest hop; export asks for `frameIndex / fps` exactly.

**Replaces (as of `cf7e88f`):** `SoundEngine::step(dt)` as the renderer's entry point. `AudioPresentationClock`, `ClockSegment` and `SinkClockDriver` (written, never read) are deleted; `PlayoutClock` is the presentation map this reads.

**fosfora:** `audio/interp.rs` (playhead 1.5 hops behind the newest frame; beat and bar phase PLLs), `audio/sync_delay.rs`.

**Test that proves it:** the same hop frames read at 30, 60 and 120 fps give the same values at the same sample positions, and every pulse appears exactly once at each rate.
