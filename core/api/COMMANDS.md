# One typed command channel (target, not built)

As of `cf7e88f` the shell reaches the renderer through 99 `external fun`s on `GeodeNative` and nine different paths: `@Volatile` fields copied each frame, `queueEvent` closures, direct renderer calls from composables, one-shot `SharedFlow`s, process-wide globals pushed by data stores (`geode_macros_set`, `geode_director_set`, `geode_note_response_set`, `geode_hand_set`), services rebuilding config from prefs, and engine → UI callbacks. A live frame makes about 13 JNI calls whether or not anything changed. Config crosses as positional float arrays with enum ordinals (`GEODE_LFO_CONFIG_FLOATS`, `GEODE_REACT_ROUTE_FLOATS`, `geode_api.h:19-25`).

**Target**
- Handles stay: `geode_audio` (a source and its analysis thread), `geode_viz` (a renderer), plus the existing `geode_dsp`, `geode_tags`, `geode_resampler`, `geode_loudness`.
- One channel per renderer: `geode_viz_command(viz, const uint8_t* msg, size_t len)`. Each message is a versioned, typed record (`SetBase{param id, value}`, `SetRoutes{…}`, `SetTimeline{…}`, `SetStyle{id}`, `SetLayer{…}`, `SetTransition{…}`, `SetMacros{…}`, `SetDirector{…}`, `Touch{…}`, `Tuning{…}`). The renderer drains its queue once at the start of a frame. No process-wide engine state.
- Events back: one poll, `geode_viz_events(viz, out, cap)` (director style moment, liquid mode, errors).
- The record layouts are generated from the schemas, so Kotlin encoders cannot drift.


**Test that proves it:** a JVM test encodes every command and a host test decodes it to the same values; a live frame with no changes makes one JNI call plus `render`.
