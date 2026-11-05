# core/sound/interp (skeleton, empty)

Part of the target in [ADR 0006](../../../docs/adr/0006-fosfora-aligned-engine.md). Nothing here yet.

**Owns:** how a renderer reads sound. It asks for the frame at a presentation time (a sample position) and gets the two hop frames around it blended by each field's policy from `core/sound/schema`: lerp for levels, hold for clocks and states, latch for one-hop pulses so none is lost or doubled.

**Contract**
- In: the frame ring from `core/sound/hop`; the presentation position (the player's output position minus the sink latency, plus a user A/V offset).
- Out: one `GeodeSoundFrame` per drawn frame, identical in shape to today's.
- Live reads behind the newest hop; export asks for `frameIndex / fps` exactly.

**Replaces (as of `cf7e88f`):** `SoundEngine::step(dt)` as the renderer's entry point; `AudioPresentationClock`, `ClockSegment` and `SinkClockDriver` (written, never read: `app/.../playback/PlaybackEngine.kt:44-46`) either become the presentation map this reads, or are deleted.

**fosfora:** `audio/interp.rs` (playhead 1.5 hops behind the newest frame; beat and bar phase PLLs), `audio/sync_delay.rs`.

**Test that proves it:** the same hop frames read at 30, 60 and 120 fps give the same values at the same sample positions, and every pulse appears exactly once at each rate.
