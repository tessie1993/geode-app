// Sound events and band regions for a style that wants to react inside the
// shader, beyond what the reaction routes already do to its SceneParams.
//
// ShaderScene steps its own viz/Reactions ReactSignals from the same feature
// frame the routes hear, so these match the Reactions tab's source list.
// Every event is an envelope: instant rise, exponential fall (0.2-2.5 s), so
// a hit can drive growth, spin or a hue jump without snapping. Do not drive
// full-frame brightness from these; the flash clamp is applied to params, not
// to what a shader does with a uniform. Move geometry and hue instead.
//
// A style includes this after lib_scene_uniforms.

/** kick, snare, hat (normalised drum levels) and onset (rising-edge envelope, 0.22 s). */
uniform vec4 uHits;
/** beat (0.3 s), drop (2.5 s), section change (1.5 s), pitch change (0.5 s). */
uniform vec4 uEvents;
/** sub, bass, low-mid, mid: band regions, each normalised to its own running peak, 0..1. */
uniform vec4 uBandsLo;
/** high-mid, presence, air, spectral flux, 0..1. */
uniform vec4 uBandsHi;
/** A monotonic count of onsets, for hashing a new variation per hit (seed = fract(uHitCount * 0.618)). */
uniform float uHitCount;
/** A monotonic count of section changes: hash it for a variation that holds until the next section. */
uniform float uSectionCount;
