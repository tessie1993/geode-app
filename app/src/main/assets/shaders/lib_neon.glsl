// lib_neon: what the neon set shares (Glow Vortex, Neon Chrome Sea, Black
// Hole, Hex Hive, Mandelbox Cathedral). Include it after lib_scene_uniforms,
// lib_scene_motion and lib_palette.
//
// ---- why the controls are read through here ---------------------------------
//
// Each of the five draws something of its own (a spiral web, a sea, a lensed
// disc, a hex bore, a fractal nave), so none of them has a "warp" or a "morph"
// in the sense the flat styles do. But the user's Customize sliders, the
// macros (Complexity, Chaos, Energy, Mood), the auto-director and the reaction
// DNA all speak only in the shared SceneParams, so a style that ignores them
// is a style where half the app goes dead. Each style therefore #defines
// VIEW_OWNS_SHAPE (lib_scene_uniforms then stops bending its rays a second
// time) and spends the shared controls on its own geometry, always through the
// normalized reads below: every one is 0..1 (twist -1..1) at the range its
// slider covers, and 0 is the look the style was tuned at. One meaning per
// read, the same in all five:
//
//   neonForm()   Morph       the shape's own metamorphosis: how far the
//                            style's structure departs from its rest form
//   neonFlow()   Warp        a smooth bend of the structure itself
//   neonChaos()  Turbulence  a restless, noisy force inside the scene
//   neonGlow()   Intensity   the neon emission's extra gain (Energy macro)
//
// Twist, tile, ripple, kaleidoscope, mirror, pixelate and zoom stay with
// view(): they are views of the picture, and apply to these styles as to any.
// Colour goes through neonPal(): each style keeps its own neon ramp as its
// identity, the user's palette (and a colour map, harder) pulls it, Hue shift
// turns it, and the key of the music slides it.

/** Morph, 0..1. */
float neonForm() { return clamp(uMorph, 0.0, 1.0); }
/** Warp, 0..1. */
float neonFlow() { return clamp(uWarp, 0.0, 1.0); }
/** Turbulence (0..1.5 on its slider), as 0..1. */
float neonChaos() { return clamp(uTurbulence / 1.5, 0.0, 1.0); }
/**
 * Intensity's share of the neon emission. grade() already scales the whole
 * frame by uIntensity, so this is half of it again on the emitters only: at
 * the Energy macro's low end (0.2) the neon fades a little more than the
 * stone, at its top (1.25) it outshines it, and at the default it is 1.
 */
float neonGlow() { return 0.5 + 0.5 * clamp(uIntensity, 0.0, 2.0); }

/**
 * How far the music's key turns the set's ramps: the eased key hue, gated by
 * how sure the key detector is, so an unclear key holds the style's own hues
 * instead of wandering.
 */
float neonKeyTurn() { return 0.35 * uKeyHue * clamp(uKeyStrength, 0.0, 1.0); }

/**
 * A style's own neon colour `own`, pulled toward the user's palette at the
 * same position `t`. The procedural palettes are a pull of a third, so the
 * neon character survives a change of palette; a colour map is chosen colour
 * by colour, so it takes over most of the way.
 */
vec3 neonPal(vec3 own, float t) {
    float pull = mix(0.3, 0.85, clamp(uPalLutMix, 0.0, 1.0));
    return mix(own, pal(t), pull);
}

/** ACES filmic: keeps the neon saturated as it blooms instead of clipping to white per channel. */
vec3 neonAces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

/**
 * The set's finish: a little extra saturation, the filmic curve, the shared
 * grade. `exposure` is the style's own; neonGlow() scales the emission before
 * this, not the whole frame, so Intensity reads as "more neon", not "brighter
 * fog".
 */
vec3 neonFinish(vec3 col, float exposure) {
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    return grade(neonAces(col * exposure));
}

// ---- the sound every style in the set hears the same way ---------------------

/** The kick, as its envelope (instant rise, a short fall). */
float neonKick() { return sndShaped(clamp(SND_KICK_ENV, 0.0, 1.0)); }
/** A drop: the held surge's bump, about two seconds. */
float neonSurge() { return clamp(SND_HELD_SURGE, 0.0, 1.0); }
/** The form layer's build-up toward a drop, 0..1. */
float neonBuild() { return clamp(SND_BUILD_UP, 0.0, 1.0); }
/** Sub and bass weight, attack-smoothed, for slow swelling things. */
float neonWeight() { return sndShaped(0.5 * (SND_BASS_ATT + SND_SUB_ATT)); }
/** The hats, as an envelope, for sparkle. */
float neonSparkle() { return sndShaped(clamp(SND_HAT_ENV, 0.0, 1.0)); }
/** 1 in silence, easing to 0 over ~2 s as music plays (1 - presence): styles settle to their rest pose. */
float neonQuiet() { return 1.0 - clamp(SND_PRESENCE, 0.0, 1.0); }
