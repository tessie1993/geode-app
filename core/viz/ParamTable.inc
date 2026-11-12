// The scene params schema: every SceneParams field, written once (#346). Not a header: it holds only
// GEODE_PARAM rows, and each includer defines GEODE_PARAM, includes this file and #undefs it again.
//
//   GEODE_PARAM(kind, name, default, min, max, fade)
//
//   kind     Float, Circular (a float that wraps: the LFO never clamps it), Int or Bool. It also picks
//            the C++ type (GEODE_PARAM_TYPE_<kind> in Params.hpp).
//   name     the C++ member, the wire name (geode_viz_param_name), the preset JSON key and the Kotlin
//            property, all one spelling.
//   default  what SceneParams{} holds.
//   min, max the control range: the widest range any control uses (Customize's slider, a keyframe
//            track, an LFO or a reaction route; the LFO and the routes clamp to it). Ints and Bools
//            count choices: a Bool is 0..1, a picker its first..last index.
//   fade     Fade: lerpParams glides it (only a Float or a Circular can). Hold: it snaps to the new
//            value, as every Int and Bool does, the custom palette sentinels and the fade time itself.
//
// The row order IS the geode_viz_set_params wire: row i is wire slot i. The shell holds the frame as it is
// and finds a name's slot through these rows (ParamSchema reads the text, so a new row needs no Kotlin edit, and
// params_schema_test holds core/tests/params_schema.txt, the JVM tests' copy, to the text). Never insert or reorder:
// append, and retire a field by leaving its row where it is, deleting its meta row and listing it in Params.hpp's kRetiredParams:
// the slot keeps its place on the wire, always at its default, and no look, control, roll or modulation
// names it. core/tests/params_schema_test.cpp checks the table; geode_params_schema_text publishes it to the shell.
//
// Under each live row, its meta row (#1005 PR 4a): what the engine's tables say about the field, so
// they are read here instead of kept in hand lists of their own.
//
//   GEODE_PARAM_META(name, label, scope, flags, rollLo, rollHi)
//
//   label    the Customize control that sets the field, by its key (its native label: the text on screen, the
//            lock key and the search term); rows of one control share it ("Palette" is the palette and
//            its two hue overrides). "" for a field no control sets.
//   scope    the catalogue::Scope of that control: which styles it can move (Catalogue::scopeOf).
//   flags    kMod: an LfoTarget writes the field, so the modulation pickers and the macro bank offer
//            it (ModTargets). 0: none.
//   rollLo, rollHi
//            the range the Randomize roll draws a number from (ParamRandomizer); 0, 0 where the roll
//            draws none: a switch, a pick over the whole schema range, or a field no roll writes.
//
// A retired row has no meta row, and every live row has exactly one (ParamsFields.cpp asserts both at
// compile time).
//
// Under a picker's meta row, its choices row: the label of each value, in index order, which the shell
// shows as it is (geode_params_schema_text's choice rows) and keeps no list of its own.
//
//   GEODE_PARAM_CHOICES(name, "first", "second", ...)
//
// params_schema_test holds each row's count to its field's min..max. Each includer defines the macros
// it reads; the others expand to nothing.
#ifndef GEODE_PARAM
#define GEODE_PARAM(kind, name, def, lo, hi, fade)
#define GEODE_PARAM_TABLE_OWN_PARAM
#endif
#ifndef GEODE_PARAM_META
#define GEODE_PARAM_META(name, label, scope, flags, rollLo, rollHi)
#define GEODE_PARAM_TABLE_OWN_META
#endif
#ifndef GEODE_PARAM_CHOICES
#define GEODE_PARAM_CHOICES(name, ...)
#define GEODE_PARAM_TABLE_OWN_CHOICES
#endif

GEODE_PARAM(Float, speed, 1.0f, 0.05f, 4.0f, Fade)
GEODE_PARAM_META(speed, "Speed", SceneClock, kMod, 0.2f, 2.5f)
// A pinch scales Zoom and a twist turns Rotation, held to the catalogue's touch ranges.
GEODE_PARAM(Float, zoom, 1.0f, catalogue::kTouchZoom.min, catalogue::kTouchZoom.max, Fade)
GEODE_PARAM_META(zoom, "Zoom", Universal, kMod, 0.6f, 2.0f)
GEODE_PARAM(Float, rotation, 0.0f, catalogue::kTouchRotation.min, catalogue::kTouchRotation.max, Fade)
GEODE_PARAM_META(rotation, "Rotation", Universal, kMod, -1.5f, 1.5f)
GEODE_PARAM(Bool, endlessZoom, false, 0, 1, Hold)
GEODE_PARAM_META(endlessZoom, "Endless zoom", EndlessZoom, 0, 0, 0)
GEODE_PARAM(Float, endlessZoomSpeed, 0.3f, -1.2f, 1.2f, Fade)
GEODE_PARAM_META(endlessZoomSpeed, "Dive speed", EndlessZoom, kMod, 0.1f, 0.8f)
GEODE_PARAM(Float, sway, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(sway, "Sway", Universal, kMod, 0.1f, 0.8f)
// Retired (SceneParams::retired): no reader since wave three.
GEODE_PARAM(Float, pulse, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, driftX, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(driftX, "Drift X", Universal, kMod, -0.5f, 0.5f)
GEODE_PARAM(Float, driftY, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(driftY, "Drift Y", Universal, kMod, -0.5f, 0.5f)
// Retired (SceneParams::retired): no reader since wave three.
GEODE_PARAM(Float, shake, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, audioDrive, 1.0f, 0.2f, 2.5f, Fade)
GEODE_PARAM_META(audioDrive, "Audio drive", AudioDrive, 0, 0.6f, 1.8f)
// Retired (SceneParams::retired): it only scaled the hidden hit reactions some scenes played on their own
// (MilkDrop's zoom kick, the simulations' beat pulses), which are gone: what moves on a hit is the
// reaction matrix alone (owner, 2026-11-12).
GEODE_PARAM(Float, beatResponse, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM(Float, turbulence, 0.0f, 0.0f, 1.5f, Fade)
GEODE_PARAM_META(turbulence, "Turbulence", Turbulence, kMod, 0.1f, 1.0f)
GEODE_PARAM(Float, density, 1.0f, 0.1f, 1.0f, Fade)
GEODE_PARAM_META(density, "Density", DyeDensity, 0, 0.4f, 1.0f)
// The user's Detail control; ShaderScene turns it into the march step budget.
GEODE_PARAM(Float, marchDetail, 1.0f, catalogue::kMarchDetail.min, catalogue::kMarchDetail.max, Fade)
GEODE_PARAM_META(marchDetail, "Detail", MarchDetail, 0, 0, 0)
GEODE_PARAM(Bool, trails, true, 0, 1, Hold)
GEODE_PARAM_META(trails, "Trails", TrailToggle, 0, 0, 0)
GEODE_PARAM(Float, trailLength, 0.5f, 0.05f, 0.98f, Fade)
GEODE_PARAM_META(trailLength, "Trail length", TrailLength, kMod, 0.3f, 0.9f)
GEODE_PARAM(Float, trailZoom, 0.0f, -0.5f, 0.5f, Fade)
GEODE_PARAM_META(trailZoom, "Trail zoom (echo in/out)", TrailEcho, 0, -0.3f, 0.3f)
GEODE_PARAM(Float, trailWarp, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(trailWarp, "Trail warp (liquid echo)", TrailEcho, 0, 0.1f, 0.6f)
GEODE_PARAM(Bool, mirror, false, 0, 1, Hold)
GEODE_PARAM_META(mirror, "Mirror", Universal, 0, 0, 0)
GEODE_PARAM(Float, warp, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(warp, "Domain warp", Universal, kMod, 0.1f, 0.8f)
GEODE_PARAM(Float, ripple, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(ripple, "Ripple", Universal, kMod, 0.1f, 0.8f)
// Retired (SceneParams::retired): the old Kaleidoscope switch and its fold count, replaced by kaleidoFolds (one
// slider a wave, an envelope, a route and a knob can move). A look saved with them still opens as it was: on
// load, readParamsJson turns them into kaleidoFolds (SceneParams::kaleidoFoldsOf).
GEODE_PARAM(Int, symmetry, SceneParams::kDefaultSymmetryFolds, 0, 16, Hold)
GEODE_PARAM(Bool, kaleidoscope, false, 0, 1, Hold)
GEODE_PARAM(Float, morph, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(morph, "Morph", ShaderLook, kMod, 0.1f, 0.8f)
GEODE_PARAM(Float, pixelate, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(pixelate, "Pixelate", Universal, kMod, 0.2f, 0.6f)
GEODE_PARAM(Float, posterize, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(posterize, "Posterize", Universal, kMod, 0.2f, 0.6f)
// Seven sprite shapes: Dot, Ring, Star, Square, Spark, Hex, Bubble.
GEODE_PARAM(Int, particleShape, 0, 0, 6, Hold)
GEODE_PARAM_META(particleShape, "Particle shape", ParticleSprite, 0, 0, 6)
GEODE_PARAM_CHOICES(particleShape, "Dot", "Ring", "Star", "Square", "Spark", "Hex", "Bubble")
GEODE_PARAM(Float, particleSize, 1.0f, 0.3f, 2.5f, Fade)
GEODE_PARAM_META(particleSize, "Particle size", ParticleSprite, kMod, 0.5f, 1.8f)
GEODE_PARAM(Float, tile, 1.0f, 1.0f, 6.0f, Fade)
GEODE_PARAM_META(tile, "Tile", Universal, kMod, 2.0f, 4.0f)
GEODE_PARAM(Float, twist, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(twist, "Twist", Universal, kMod, -0.8f, 0.8f)
// An index into SceneParams::palettes().
GEODE_PARAM(Int, palette, 0, 0, static_cast<float>(SceneParams::palettes().size() - 1), Hold)
GEODE_PARAM_META(palette, "Palette", Universal, 0, 0, 0)
// Retired (SceneParams::retired): Palette 2 and Palette blend, the old second palette mixed over the first.
// The palette stack (viz/Palette.hpp) is the one way to draw more than one palette.
GEODE_PARAM(Int, palette2, 1, 0, static_cast<float>(SceneParams::palettes().size() - 1), Hold)
GEODE_PARAM(Float, paletteMix, 0.0f, 0.0f, 1.0f, Fade)
// A custom palette's hue base and span, 0..1; kUnsetOverride (-1) = use the palette's own. Never
// faded: a glide through the sentinel would flash the palette's own colours mid-fade.
GEODE_PARAM(Float, paletteBaseOverride, SceneParams::kUnsetOverride, SceneParams::kUnsetOverride, 1.0f, Hold)
GEODE_PARAM_META(paletteBaseOverride, "Palette", Universal, 0, 0, 0)
GEODE_PARAM(Float, paletteRangeOverride, SceneParams::kUnsetOverride, SceneParams::kUnsetOverride, 1.0f, Hold)
GEODE_PARAM_META(paletteRangeOverride, "Palette", Universal, 0, 0, 0)
// Retired with Palette 2: its hue overrides.
GEODE_PARAM(Float, palette2BaseOverride, SceneParams::kUnsetOverride, SceneParams::kUnsetOverride, 1.0f, Hold)
GEODE_PARAM(Float, palette2RangeOverride, SceneParams::kUnsetOverride, SceneParams::kUnsetOverride, 1.0f, Hold)
// Retired (SceneParams::retired): Colour map, the five cyclic colour maps as a list of their own. They are
// palettes of the stack's library now (PaletteTable.inc rows 21..25), so Look has one palette list.
GEODE_PARAM(Int, paletteLut, SceneParams::kNoPaletteLut, SceneParams::kNoPaletteLut, 4, Hold)
GEODE_PARAM(Float, milkdropPaletteTint, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(milkdropPaletteTint, "MilkDrop palette tint", Milkdrop, 0, 0.2f, 0.9f)
GEODE_PARAM(Bool, milkdropBlendPresets, false, 0, 1, Hold)
GEODE_PARAM_META(milkdropBlendPresets, "Blend preset changes", Milkdrop, 0, 0, 0)
// A hue turn: 1 is the full circle, and the LFO adds to it unclamped.
GEODE_PARAM(Circular, colorShift, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(colorShift, "Hue shift", Universal, kMod, 0.0f, 1.0f)
GEODE_PARAM(Float, hueRange, 1.0f, 0.0f, 1.5f, Fade)
GEODE_PARAM_META(hueRange, "Hue range", Universal, 0, 0.5f, 1.5f)
GEODE_PARAM(Float, saturation, 1.0f, 0.0f, 1.5f, Fade)
GEODE_PARAM_META(saturation, "Saturation", Universal, kMod, 0.4f, 1.4f)
// The slider and keyframes stop at 1.25 (1 + the flash clamp's depth); the LFO and the routes reach 2.
GEODE_PARAM(Float, brightness, 1.0f, 0.2f, 2.0f, Fade)
GEODE_PARAM_META(brightness, "Brightness", Universal, kMod, 0.7f, 1.25f)
GEODE_PARAM(Float, contrast, 1.0f, 0.3f, 1.25f, Fade)
GEODE_PARAM_META(contrast, "Contrast", Universal, 0, 0.8f, 1.25f)
GEODE_PARAM(Float, gamma, 1.0f, 0.3f, 2.5f, Fade)
GEODE_PARAM_META(gamma, "Gamma", Universal, 0, 0.8f, 1.3f)
// Retired (SceneParams::retired): Color cycle and Cycle speed, the old hue cycle that turned every colour round
// the wheel. A wave or an envelope on Palette position walks the palette stack instead.
GEODE_PARAM(Bool, colorCycle, false, 0, 1, Hold)
GEODE_PARAM(Float, cycleSpeed, 0.1f, 0.02f, 0.6f, Fade)
GEODE_PARAM(Bool, invert, false, 0, 1, Hold)
GEODE_PARAM_META(invert, "Invert", Universal, 0, 0, 0)
// As brightness: the slider stops at 1.25, the LFO reaches 2.
GEODE_PARAM(Float, intensity, 1.0f, 0.2f, 2.0f, Fade)
GEODE_PARAM_META(intensity, "Intensity", Universal, kMod, 0.7f, 1.25f)
GEODE_PARAM(Bool, duotone, false, 0, 1, Hold)
GEODE_PARAM_META(duotone, "Duotone", ShaderLook, 0, 0, 0)
// The slider and keyframes stop at 0.25 (the flash clamp's depth); the LFO and the routes reach 1.
GEODE_PARAM(Float, bloom, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(bloom, "Bloom", Universal, kMod, 0.1f, 0.25f)
GEODE_PARAM(Float, temperature, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(temperature, "Temperature", Universal, kMod, -0.6f, 0.6f)
GEODE_PARAM(Bool, solarize, false, 0, 1, Hold)
GEODE_PARAM_META(solarize, "Solarize", Universal, 0, 0, 0)
GEODE_PARAM(Float, bassGain, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(bassGain, "Bass gain", BassBand, 0, 0.8f, 1.4f)
GEODE_PARAM(Float, midGain, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(midGain, "Mid gain", MidBand, 0, 0.8f, 1.4f)
GEODE_PARAM(Float, trebGain, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(trebGain, "Treble gain", TrebleBand, 0, 0.8f, 1.4f)
// Retired (SceneParams::retired): no reader since wave three.
GEODE_PARAM(Float, flash, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, chromaAb, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(chromaAb, "Chromatic aberration", Universal, kMod, 0.1f, 0.5f)
GEODE_PARAM(Float, vignette, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(vignette, "Vignette", Universal, kMod, 0.1f, 0.6f)
GEODE_PARAM(Float, scanlines, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(scanlines, "Scanlines", Universal, 0, 0.15f, 0.5f)
GEODE_PARAM(Float, grain, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(grain, "Film grain", Universal, 0, 0.1f, 0.4f)
// Retired: the composite Glitch effect is gone.
GEODE_PARAM(Float, glitch, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, fisheye, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(fisheye, "Fisheye", Universal, kMod, -0.5f, 0.5f)
// Retired (SceneParams::retired): the composite's strobe is gone.
GEODE_PARAM(Float, strobe, 0.0f, 0.0f, 1.0f, Fade)
// Customize › Advanced › Glide between settings, in seconds: the fade's own length, never faded.
GEODE_PARAM(Float, paramFadeSec, 0.0f, 0.0f, 5.0f, Hold)
GEODE_PARAM_META(paramFadeSec, "Glide time", Universal, 0, 0, 0)
// A fluid::quality tier index, 0 Ultra .. Min.
GEODE_PARAM(Int, fluidQuality, 2, 0, static_cast<float>(fluid::quality::kTierCount - 1), Hold)
GEODE_PARAM_META(fluidQuality, "Fluid quality", Emitters, 0, 0, 0)
GEODE_PARAM_CHOICES(fluidQuality, "Ultra", "High", "Medium", "Low", "Min")
GEODE_PARAM(Bool, fluidAutoQuality, true, 0, 1, Hold)
GEODE_PARAM_META(fluidAutoQuality, "Lower the quality when the phone struggles", Emitters, 0, 0, 0)
GEODE_PARAM(Int, fluidIterations, 20, 8, 40, Hold)
GEODE_PARAM_META(fluidIterations, "Solver iterations", FluidSim, 0, 12, 28)
GEODE_PARAM(Float, fluidPressure, 0.8f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(fluidPressure, "Pressure", FluidSim, 0, 0.5f, 0.95f)
GEODE_PARAM(Float, fluidCurl, 30.0f, 0.0f, 50.0f, Fade)
GEODE_PARAM_META(fluidCurl, "Fluid curl", FluidSim, kMod, 5.0f, 45.0f)
GEODE_PARAM(Float, fluidVelocityDissipation, 0.2f, 0.0f, 4.0f, Fade)
GEODE_PARAM_META(fluidVelocityDissipation, "Motion fade", FluidSim, 0, 0.02f, 0.6f)
GEODE_PARAM(Float, fluidDensityDissipation, 1.0f, 0.0f, 4.0f, Fade)
GEODE_PARAM_META(fluidDensityDissipation, "Fluid fade", FluidSim, kMod, 0.2f, 2.2f)
GEODE_PARAM(Float, fluidChromaticAging, 0.3f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(fluidChromaticAging, "Chromatic aging", FluidSim, 0, 0.0f, 0.8f)
GEODE_PARAM(Float, fluidSplatRadius, 0.12f, 0.02f, 0.4f, Fade)
GEODE_PARAM_META(fluidSplatRadius, "Fluid splat radius", Emitters, kMod, 0.05f, 0.25f)
GEODE_PARAM(Float, fluidSplatForce, 1.0f, 0.0f, 3.0f, Fade)
GEODE_PARAM_META(fluidSplatForce, "Fluid splat force", Emitters, kMod, 0.5f, 2.0f)
// Center, Ring, Random, Spectrum (FluidEmitters clamps to the same).
GEODE_PARAM(Int, fluidBeatPattern, 1, 0, 3, Hold)
GEODE_PARAM_META(fluidBeatPattern, "Hit pattern", Emitters, 0, 0, 3)
GEODE_PARAM_CHOICES(fluidBeatPattern, "Center", "Ring", "Random", "Spectrum")
GEODE_PARAM(Int, fluidBeatSplats, 3, 0, 8, Hold)
GEODE_PARAM_META(fluidBeatSplats, "Hit splats", Emitters, 0, 1, 6)
GEODE_PARAM(Int, fluidStirrers, 2, 0, 4, Hold)
GEODE_PARAM_META(fluidStirrers, "Stirrers", Emitters, 0, 0, 3)
GEODE_PARAM(Float, fluidStirrerSpeed, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(fluidStirrerSpeed, "Stirrer speed", Emitters, 0, 0.3f, 1.6f)
GEODE_PARAM(Bool, fluidBassPump, false, 0, 1, Hold)
GEODE_PARAM_META(fluidBassPump, "Bass pump", Emitters, 0, 0, 0)
GEODE_PARAM(Float, fluidPaletteCycleSpeed, 0.5f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(fluidPaletteCycleSpeed, "Palette cycle", FluidSim, 0, 0.0f, 1.2f)
GEODE_PARAM(Bool, fluidSparkle, true, 0, 1, Hold)
GEODE_PARAM_META(fluidSparkle, "Treble sparkle", Emitters, 0, 0, 0)
// Orbit, Lissajous, Rose, Bloom, Drift (fluid::Choreography::kPathCount).
GEODE_PARAM(Int, fluidSpawnPath, 1, 0, 4, Hold)
GEODE_PARAM_META(fluidSpawnPath, "Path", Journey, 0, 0, 4)
GEODE_PARAM_CHOICES(fluidSpawnPath, "Orbit", "Lissajous", "Rose", "Bloom", "Drift")
GEODE_PARAM(Int, fluidSpawnPoints, 3, 1, 8, Hold)
GEODE_PARAM_META(fluidSpawnPoints, "Spawn points", Journey, 0, 2, 5)
GEODE_PARAM(Float, fluidSpawnProgress, 1.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(fluidSpawnProgress, "Progression", Journey, 0, 0, 0)
GEODE_PARAM(Int, fluidCatchPoints, 2, 0, 4, Hold)
GEODE_PARAM_META(fluidCatchPoints, "Catch points", Journey, 0, 0, 3)
GEODE_PARAM(Float, fluidCatchPull, 1.0f, 0.0f, 3.0f, Fade)
GEODE_PARAM_META(fluidCatchPull, "Catch pull", Journey, kMod, 0.4f, 1.8f)
GEODE_PARAM(Float, fluidCatchRadius, 0.12f, 0.03f, 0.3f, Fade)
GEODE_PARAM_META(fluidCatchRadius, "Catch radius", Journey, kMod, 0.06f, 0.2f)
GEODE_PARAM(Bool, fluidParticlesEnabled, true, 0, 1, Hold)
GEODE_PARAM_META(fluidParticlesEnabled, "Particle layer", FluidSim, 0, 0, 0)
GEODE_PARAM(Float, fluidParticleLife, 6.0f, 1.0f, 20.0f, Fade)
GEODE_PARAM_META(fluidParticleLife, "Particle life (s)", ParticleSprite, 0, 3.0f, 12.0f)
GEODE_PARAM(Float, fluidParticleDrag, 0.5f, 0.02f, 1.0f, Fade)
GEODE_PARAM_META(fluidParticleDrag, "Particle drag", ParticleSprite, 0, 0.15f, 0.9f)
GEODE_PARAM(Float, fluidParticleBrightness, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(fluidParticleBrightness, "Particle brightness", FluidSim, 0, 0.6f, 1.6f)
GEODE_PARAM(Bool, fluidDyeEnabled, true, 0, 1, Hold)
GEODE_PARAM_META(fluidDyeEnabled, "Ink layer", FluidSim, 0, 0, 0)
GEODE_PARAM(Bool, fluidShading, true, 0, 1, Hold)
GEODE_PARAM_META(fluidShading, "Shading (embossed ink)", FluidSim, 0, 0, 0)
GEODE_PARAM(Bool, fluidBloom, true, 0, 1, Hold)
GEODE_PARAM_META(fluidBloom, "Glow (fluid)", FluidSim, 0, 0, 0)
GEODE_PARAM(Float, fluidBloomIntensity, 0.8f, 0.1f, 2.0f, Fade)
GEODE_PARAM_META(fluidBloomIntensity, "Fluid glow", FluidSim, kMod, 0.4f, 1.4f)
GEODE_PARAM(Float, fluidBloomThreshold, 0.6f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(fluidBloomThreshold, "Glow threshold", FluidSim, 0, 0.4f, 0.8f)
GEODE_PARAM(Bool, fluidSunrays, true, 0, 1, Hold)
GEODE_PARAM_META(fluidSunrays, "Sunrays", FluidSim, 0, 0, 0)
GEODE_PARAM(Float, fluidSunraysWeight, 1.0f, 0.3f, 1.0f, Fade)
GEODE_PARAM_META(fluidSunraysWeight, "Sunrays weight", FluidSim, 0, 0.4f, 1.0f)
GEODE_PARAM(Float, fluidCurlAudio, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(fluidCurlAudio, "Curl from mids", FluidSim, 0, 0.1f, 0.9f)
GEODE_PARAM(Float, fluidBloomAudio, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(fluidBloomAudio, "Glow from loudness", FluidSim, 0, 0.1f, 0.9f)
GEODE_PARAM(Float, fluidFadeAudio, 0.6f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(fluidFadeAudio, "Fade when quiet", FluidSim, 0, 0.2f, 0.9f)
GEODE_PARAM(Float, fluidRadiusPulse, 0.4f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(fluidRadiusPulse, "Radius on hit", Emitters, 0, 0.0f, 0.8f)
// Liquid mode (viz/liquid; Customize › Advanced › Liquid, Pro): the switch is flowEnabled, Liquidity is
// flowStrength, Push is flowForce and Swirl is flowCurl, the old flow field's fields, so a preset saved
// with "Flow field on" opens as a liquid.
GEODE_PARAM(Bool, flowEnabled, false, 0, 1, Hold)
GEODE_PARAM_META(flowEnabled, "Flow field on", Universal, 0, 0, 0)
GEODE_PARAM(Float, flowStrength, 0.35f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(flowStrength, "Flow strength", Universal, kMod, 0.1f, 0.7f)
GEODE_PARAM(Float, flowForce, 1.0f, 0.0f, 3.0f, Fade)
GEODE_PARAM_META(flowForce, "Flow force", FlowFieldSim, kMod, 0.4f, 2.0f)
GEODE_PARAM(Float, flowCurl, 25.0f, 0.0f, 50.0f, Fade)
GEODE_PARAM_META(flowCurl, "Flow curl", FlowFieldSim, kMod, 5.0f, 40.0f)
// Retired (SceneParams::retired): the Water style's knobs. Pool replaced Water and reads none of them.
GEODE_PARAM(Float, waterWaveSpeed, 1.0f, 0.2f, 2.0f, Fade)
GEODE_PARAM(Float, waterDamping, 0.985f, 0.9f, 0.999f, Fade)
GEODE_PARAM(Float, waterRippleStrength, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM(Float, waterDepth, 0.6f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, waterSpecular, 0.7f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, waterFlow, 0.3f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, waterLiquid, 0.85f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, waterLiquidFlow, 1.4f, 0.0f, 4.0f, Fade)
GEODE_PARAM(Float, waterLiquidFade, 0.35f, 0.0f, 2.0f, Fade)
// Water dish, Chladni plate.
GEODE_PARAM(Int, cymaticsGeometry, 0, 0, 1, Hold)
GEODE_PARAM_META(cymaticsGeometry, "Geometry", Cymatics, 0, 0, 1)
GEODE_PARAM_CHOICES(cymaticsGeometry, "Water dish", "Chladni plate")
GEODE_PARAM(Float, cymaticsFundamental, 110.0f, cymatics::kMinFundamentalHz, cymatics::kMaxFundamentalHz, Fade)
GEODE_PARAM_META(cymaticsFundamental, "Fundamental (Hz)", Cymatics, 0, 55.0f, 260.0f)
GEODE_PARAM(Int, cymaticsModes, 5, 1, static_cast<float>(cymatics::kMaxRenderedModes), Hold)
GEODE_PARAM_META(cymaticsModes, "Standing waves", Cymatics, 0, 2, cymatics::kMaxRenderedModes)
GEODE_PARAM(Float, cymaticsRing, 0.35f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(cymaticsRing, "Plate ring", Cymatics, 0, 0.15f, 0.8f)
GEODE_PARAM(Float, cymaticsFocus, 0.7f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(cymaticsFocus, "Tonal focus", Cymatics, 0, 0.35f, 1.0f)
GEODE_PARAM(Float, cymaticsScale, 3.2f, 0.5f, 8.0f, Fade)
GEODE_PARAM_META(cymaticsScale, "Field scale", Cymatics, 0, 1.6f, 5.5f)
GEODE_PARAM(Float, cymaticsFill, 0.45f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(cymaticsFill, "Fill", Cymatics, 0, 0.05f, 1.0f)
GEODE_PARAM(Float, cymaticsLine, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(cymaticsLine, "Nodal lines", Cymatics, 0, 0.4f, 1.7f)
GEODE_PARAM(Float, cymaticsGlow, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(cymaticsGlow, "Nodal glow", Cymatics, 0, 0.3f, 1.7f)
GEODE_PARAM(Float, cymaticsIridescence, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(cymaticsIridescence, "Iridescence", Cymatics, 0, 0.0f, 1.0f)
GEODE_PARAM(Float, cymaticsCaustic, 0.8f, 0.0f, 1.5f, Fade)
GEODE_PARAM_META(cymaticsCaustic, "Caustic sheen", Cymatics, 0, 0.2f, 1.3f)
GEODE_PARAM(Float, cymaticsFlow, 0.35f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(cymaticsFlow, "Wave flow", Cymatics, 0, 0.05f, 0.8f)
GEODE_PARAM(Float, cymaticsSwirl, 0.05f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(cymaticsSwirl, "Field swirl", Cymatics, 0, -0.3f, 0.3f)
// Retired (SceneParams::retired): the ripple overlay, removed with Water.
GEODE_PARAM(Bool, rippleOverlayEnabled, false, 0, 1, Hold)
GEODE_PARAM(Float, rippleOverlayStrength, 0.4f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, rippleOverlaySpecular, 0.3f, 0.0f, 1.0f, Fade)
// Retired: the superformula driver (viz/FormDrive) it scaled is gone since wave three.
GEODE_PARAM(Float, formDrive, 0.7f, 0.0f, 1.0f, Fade)

// Wave three: the continuous motion system (viz/MotionField), which replaced formDrive, flash, strobe,
// pulse and shake as the way the music moves a scene. Appended after the older fields so every existing wire
// index survives.
GEODE_PARAM(Float, motionAmount, 0.7f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(motionAmount, "Motion amount", Universal, 0, 0.3f, 1.0f)
GEODE_PARAM(Float, motionBreath, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(motionBreath, "Breath", Universal, 0, 0.2f, 0.9f)
// Retired: never had a reader or a control.
GEODE_PARAM(Float, motionOrbit, 0.5f, 0.0f, 1.0f, Fade)
// Spin: how far a turning style (catalogue::spinRate) turns with the mids; 0 holds it still. The old Spin
// drift control under its wire name, so a saved look keeps its value. Shown only where a style turns, and
// never rolled: whether a style spins is the user's choice (owner, 2026-11-12).
GEODE_PARAM(Float, motionDrift, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(motionDrift, "Spin", Spin, 0, 0, 0)
GEODE_PARAM(Float, motionHue, 0.4f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(motionHue, "Key colour", Universal, 0, 0.1f, 0.8f)

// The shared camera rig (viz/CameraRig) and the reaction matrix (viz/Reactions). Appended after wave
// three so every wire index survives. The cam* offsets (push, yaw, pitch, roll) are what reaction
// routes and LFOs write; the rig springs toward them, so a route never snaps the view.
// Travel speed along the path, negative backward: the slider stops at -2..2, the LFO and the routes reach 2.5.
GEODE_PARAM(Float, camFly, 0.5f, -2.5f, 2.5f, Fade)
GEODE_PARAM_META(camFly, "Fly speed", Universal, kMod, 0.2f, 1.4f)
// 0 glide, 1 spiral, 2 orbit, 3 hover, 4 drive, 5 fall, 6 rise, 7 weave, 8 corkscrew, 9 swoop, 10 strafe,
// 11 pendulum (CameraRig clamps to the same).
GEODE_PARAM(Int, camPath, 0, 0, 11, Hold)
GEODE_PARAM_META(camPath, "Camera path", Universal, 0, 0, 0)
GEODE_PARAM_CHOICES(camPath, "Glide", "Spiral", "Orbit", "Hover", "Drive", "Fall", "Rise", "Weave", "Corkscrew", "Swoop", "Strafe",
                    "Pendulum")
// Sideways swing, or the orbit's radius.
GEODE_PARAM(Float, camOrbit, 0.35f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(camOrbit, "Swing", Universal, kMod, 0.1f, 0.8f)
// The dolly offset.
GEODE_PARAM(Float, camPush, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(camPush, "Push in", Universal, kMod, 0, 0)
// The look offsets.
GEODE_PARAM(Float, camYaw, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(camYaw, "Look turn", Universal, kMod, 0, 0)
GEODE_PARAM(Float, camPitch, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(camPitch, "Look tilt", Universal, kMod, 0, 0)
GEODE_PARAM(Float, camRoll, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(camRoll, "Roll", Universal, kMod, 0, 0)
// A field-of-view multiplier.
GEODE_PARAM(Float, camFov, 1.0f, 0.5f, 2.0f, Fade)
GEODE_PARAM_META(camFov, "Field of view", Universal, kMod, 0.8f, 1.4f)
// Handheld wobble.
GEODE_PARAM(Float, camShake, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(camShake, "Handheld shake", Universal, kMod, 0.1f, 0.5f)
// Spring softness: 0 tight .. 1 floaty.
GEODE_PARAM(Float, camSmooth, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(camSmooth, "Camera smoothing", Universal, 0, 0, 0)
// Drag to look around.
GEODE_PARAM(Bool, camTouch, false, 0, 1, Hold)
GEODE_PARAM_META(camTouch, "Drag to look", Universal, 0, 0, 0)
// Phone tilt steers the view (never in an export).
GEODE_PARAM(Bool, camGyro, false, 0, 1, Hold)
GEODE_PARAM_META(camGyro, "Tilt phone to look", Universal, 0, 0, 0)
// The global scale of every reaction route.
GEODE_PARAM(Float, reactAmount, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(reactAmount, "Reaction strength", Universal, 0, 0, 0)
// Which variant of the style's reaction DNA. It travels as a float, so it stops at 2^24 - 1, the last
// int a float carries exactly.
GEODE_PARAM(Int, reactSeed, 0, 0, 16777215, Hold)
GEODE_PARAM_META(reactSeed, "Reaction DNA", Universal, 0, 1, 16777215)

// Note & key reactions (viz/NoteResponse), per style like every Customize control, so a preset, a
// template and an export carry them. Appended so every existing wire index survives. Inert while
// Settings › Behaviour › Note & key reactions is off: the layer then does nothing at all.
// Customize › Music › Notes & key (Pro): how much each part feeds the style knobs, 0 none .. 1 as
// designed .. 2.
// Read by nothing: it was the tint toward the notes' colour, which is gone. Kept so the wire indices
// and saved looks survive.
GEODE_PARAM(Float, noteColor, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(noteColor, "@notes_key", Universal, 0, 0, 0)
// Pitch height: the TONE knob.
GEODE_PARAM(Float, notePitch, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(notePitch, "@notes_key", Universal, 0, 0, 0)
// Leaps and steps: the swing and glide behind the SPACE knob.
GEODE_PARAM(Float, noteIntervals, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(noteIntervals, "@notes_key", Universal, 0, 0, 0)
// Harmonic tension and builds: the BUILD knob.
GEODE_PARAM(Float, noteTension, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(noteTension, "@notes_key", Universal, 0, 0, 0)
// A chord change turns the swing behind the SPACE knob.
GEODE_PARAM(Float, noteChords, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(noteChords, "@notes_key", Universal, 0, 0, 0)
// The drums' push on the PULSE knob, on top of how percussive the track is.
GEODE_PARAM(Float, noteDrums, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(noteDrums, "@notes_key", Universal, 0, 0, 0)

// Liquid mode's pickers (viz/liquid). Appended so every existing wire index survives.
// What forms at the vanishing point: liquid::Born (0 live, 1 re-rolled, 2 next style, 3 ink forms).
GEODE_PARAM(Int, liquidBorn, 1, 0, 3, Hold)
GEODE_PARAM_META(liquidBorn, "Born inside", Universal, 0, 0, 0)
GEODE_PARAM_CHOICES(liquidBorn, "Live style", "Style re-rolled", "Next style", "Forms")
// liquid::Age: 0 pure palette, 1 fade deep, 2 ink bleed.
GEODE_PARAM(Int, liquidAge, 0, 0, 2, Hold)
GEODE_PARAM_META(liquidAge, "Colour age", Universal, 0, 0, 0)
GEODE_PARAM_CHOICES(liquidAge, "Pure palette", "Fade deep", "Ink bleed")
// liquid::Surface: 0 ink, 1 wet gloss, 2 glass.
GEODE_PARAM(Int, liquidSurface, 0, 0, 2, Hold)
GEODE_PARAM_META(liquidSurface, "Surface", Universal, 0, 0, 0)
GEODE_PARAM_CHOICES(liquidSurface, "Ink", "Gloss", "Glass")

// Camera travel for the flat (2D) styles: how much Fly speed (its sign too) zooms the picture in or
// out without end, 0 off .. 1 (CameraRig::flatDive). Appended so every wire index survives.
GEODE_PARAM(Float, camFlat, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(camFlat, "Travel zoom", Universal, 0, 0, 0)

// Your colours (Customize › Look): the first palette's own colour stops, each a packed 0xRRGGBB
// (viz/Colour.hpp), kNoStop (-1) unset. Set from paletteStop0 on, they replace that slot's ramp: the
// stops evenly round a loop (lib_palette's palStops), the first stop's hue for the styles that colour by
// one hue. Appended so every wire index survives; a float carries 0xFFFFFF exactly, as reactSeed's max.
// They belong to the Palette control: its roll and Reset clear them, Keep holds them, borrow copies them.
GEODE_PARAM(Int, paletteStop0, SceneParams::kNoStop, SceneParams::kNoStop, 16777215, Hold)
GEODE_PARAM_META(paletteStop0, "Palette", Universal, 0, 0, 0)
GEODE_PARAM(Int, paletteStop1, SceneParams::kNoStop, SceneParams::kNoStop, 16777215, Hold)
GEODE_PARAM_META(paletteStop1, "Palette", Universal, 0, 0, 0)
GEODE_PARAM(Int, paletteStop2, SceneParams::kNoStop, SceneParams::kNoStop, 16777215, Hold)
GEODE_PARAM_META(paletteStop2, "Palette", Universal, 0, 0, 0)
GEODE_PARAM(Int, paletteStop3, SceneParams::kNoStop, SceneParams::kNoStop, 16777215, Hold)
GEODE_PARAM_META(paletteStop3, "Palette", Universal, 0, 0, 0)
GEODE_PARAM(Int, paletteStop4, SceneParams::kNoStop, SceneParams::kNoStop, 16777215, Hold)
GEODE_PARAM_META(paletteStop4, "Palette", Universal, 0, 0, 0)

// Endless zoom's tunnel (viz/EndlessZoom.hpp, tunnel::Look; Customize › Dive). Appended so every wire
// index survives. Each default is the plain tunnel, so a look saved before them dives as it did.
// Octaves between two nested copies.
GEODE_PARAM(Float, diveSpacing, 1.0f, 0.5f, 2.0f, Fade)
GEODE_PARAM_META(diveSpacing, "Ring spacing", EndlessZoom, 0, 0.7f, 1.6f)
// Quarter turns a copy makes per octave it grows: the tunnel as a spiral.
GEODE_PARAM(Float, diveTwist, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(diveTwist, "Dive twist", EndlessZoom, kMod, -0.6f, 0.6f)
// The vanishing point, -1..1 across the frame. Where the user points the dive: no roll draws it.
GEODE_PARAM(Float, diveAimX, 0.0f, -0.6f, 0.6f, Fade)
GEODE_PARAM_META(diveAimX, "Dive aim X", EndlessZoom, kMod, 0, 0)
GEODE_PARAM(Float, diveAimY, 0.0f, -0.6f, 0.6f, Fade)
GEODE_PARAM_META(diveAimY, "Dive aim Y", EndlessZoom, kMod, 0, 0)
// A copy's outline: 0 round, 0.5 the rounded frame, 1 nearly square.
GEODE_PARAM(Float, diveShape, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(diveShape, "Tunnel shape", EndlessZoom, 0, 0.0f, 1.0f)
// How much of a copy's band dissolves into the copy around it: 0 a cut .. 1 the whole band. The roll
// stays in the soft half: it never deals the hard-edged frames.
GEODE_PARAM(Float, diveBlend, 0.75f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(diveBlend, "Dive blend", EndlessZoom, 0, 0.55f, 1.0f)
// How far the far end of the tunnel falls into dark.
GEODE_PARAM(Float, diveFog, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(diveFog, "Depth fade", EndlessZoom, 0, 0.2f, 0.7f)
// How much more (above 0) or less of the picture each copy's band holds.
GEODE_PARAM(Float, diveLens, 0.0f, -0.5f, 1.5f, Fade)
GEODE_PARAM_META(diveLens, "Dive lens", EndlessZoom, 0, -0.3f, 0.8f)
// The music's slow hand on the dive's rate (endless::Follow): 0 none, 1 as designed, 2 twice the reach.
// How much the user wants the music to move things: no roll draws it.
GEODE_PARAM(Float, diveFollow, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(diveFollow, "Music follow", EndlessZoom, 0, 0, 0)

// The palette stack's amounts (viz/Palette.hpp, Customize › Look): how much each layer is used, the primary
// first, beside the stack the look keeps as text. The defaults are palette::defaultAmounts(). Appended so
// every wire index survives. The stack's own roll deals them, never the param roll. Palette 2's amount is
// what LfoTarget::PaletteMix writes (the old Palette blend's slot), on shader styles.
GEODE_PARAM(Float, paletteAmount0, 1.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(paletteAmount0, "Palette 1 amount", Universal, kMod, 0, 0)
GEODE_PARAM(Float, paletteAmount1, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(paletteAmount1, "Palette 2 amount", ShaderLook, kMod, 0, 0)
GEODE_PARAM(Float, paletteAmount2, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(paletteAmount2, "Palette 3 amount", Universal, kMod, 0, 0)
GEODE_PARAM(Float, paletteAmount3, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(paletteAmount3, "Palette 4 amount", Universal, kMod, 0, 0)
GEODE_PARAM(Float, paletteAmount4, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(paletteAmount4, "Palette 5 amount", Universal, kMod, 0, 0)
GEODE_PARAM(Float, paletteAmount5, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(paletteAmount5, "Palette 6 amount", Universal, kMod, 0, 0)
GEODE_PARAM(Float, paletteAmount6, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(paletteAmount6, "Palette 7 amount", Universal, kMod, 0, 0)
GEODE_PARAM(Float, paletteAmount7, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(paletteAmount7, "Palette 8 amount", Universal, kMod, 0, 0)
// Where along the stack's ramp the colours sit: the walk Hue shift and moment colours will drive, so colour
// moves stay inside the palettes. A turn of 1 is the whole ramp; the LFO adds unclamped.
GEODE_PARAM(Circular, paletteShift, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(paletteShift, "Palette position", Universal, kMod, 0, 0)

// The kaleidoscope as one slider (Customize › Shape): 1 is off, 2..16 the folds. Appended so every wire index
// survives; it replaces the retired kaleidoscope switch and symmetry count. A float, so a wave, an envelope, a
// reaction route and a macro knob can move it (LfoTarget::KaleidoFolds); the shaders fold by its nearest whole
// count (SceneParams::kaleidoUniforms), so a slow wave steps from one count to the next without a seam. Its roll
// keeps the old switch's odds and deals a count from kSymmetryFolds (ParamRandomizer), so no roll range here.
GEODE_PARAM(Float, kaleidoFolds, 1.0f, 1.0f, 16.0f, Fade)
GEODE_PARAM_META(kaleidoFolds, "Kaleidoscope", Universal, kMod, 0, 0)

// Customize › Layers' mix as a modulation target (LfoTarget::LayerMix): what waves, envelopes, reaction routes and
// macro knobs add to the mix the Layers page sets (viz/LayerState), which stays the base. A look holds 0 here, so
// nothing moves the layer until the user routes something to it; the composite holds the sum to 0..1. Appended so
// every wire index survives. No roll deals it (the layer is the user's), and no style reads it.
GEODE_PARAM(Float, layerMix, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(layerMix, "Layer mix", Universal, kMod, 0, 0)

// The physics layers (viz/physics): a low-res fluid the finished picture seeds and stirs, drawn behind or over
// it, and particles that ride that fluid (or a curl drift with the fluid off), drawn in front: back fluid | style
// | front particles. A stage over the finished picture, so every style kind can carry them. Each mix is 0 in
// every look, so every look draws as it did and nothing is allocated; waves, envelopes, reaction routes and macro
// knobs move them (LfoTarget::SimFluid .. SimLayerOrder), the music's only way in. Appended so every wire index
// survives; no roll deals them (they are the user's), and no style reads them.
GEODE_PARAM(Float, simFluid, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(simFluid, "Fluid layer", Universal, kMod, 0, 0)
GEODE_PARAM(Float, simParticles, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(simParticles, "Particle field", Universal, kMod, 0, 0)
// How hard the picture stirs the fluid and how fast the particles drift: 1 as designed, 2 twice, 0 still.
GEODE_PARAM(Float, simForce, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM_META(simForce, "Physics force", Universal, kMod, 0, 0)
// Where the fluid sits: 0 behind the style (it shows through the style's darks), 1 over it (screened on).
GEODE_PARAM(Float, simLayerOrder, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(simLayerOrder, "Fluid in front", Universal, kMod, 0, 0)

// Grow and layers (Customize › Shape): four shape params the music can drive, each a modulation target
// (LfoTarget::Spawn .. BackLayer), so a reaction route, a wave, an envelope or a macro knob can move it. Drawn
// by the style family libraries (catalogue::Scope::FamilyForm names which), never by a colour: none of them
// touches the palette, a brightness or a contrast, and none turns the picture. Each default is the picture
// as it was, which every library keeps by taking its old path there. No roll deals them (ParamRandomizer).
// Appended after the physics layers (wire slots 193..197 with Formula) so every wire index survives.
//
// Spawn: more of the family's subject. A second copy of the world splits off the first and eases away as
// it rises (the petals, shards, threads or rosettes double, then drift between the first).
GEODE_PARAM(Float, spawn, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(spawn, "Spawn", FamilyForm, kMod, 0, 0)
// Fractal: the subject fractals out. Smaller self-similar copies of it grow on its own surface, out to a
// shell that thickens as it rises.
GEODE_PARAM(Float, fractal, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(fractal, "Fractal", FamilyForm, kMod, 0, 0)
// Front layer and Back layer: how far the foreground and the background open or draw in. 0 is as designed;
// up opens that layer, down draws it in. The layers are depth (lib_grow): flown through, what is round
// the eye and what lies down the way; turned about, the subject and what lies beyond it; a family with a
// backdrop of its own (Prism's sky, Loom's skins) moves it with the back layer. Two rows, so a route can
// move one layer alone: the subject pulses with the kick while only the backdrop breathes with the chord.
GEODE_PARAM(Float, frontLayer, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(frontLayer, "Front layer", FamilyForm, kMod, 0, 0)
GEODE_PARAM(Float, backLayer, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM_META(backLayer, "Back layer", FamilyForm, kMod, 0, 0)

// Formula (Customize › Shape › Grow): which of a raymarched style's own distance formulas draws it, 0 the
// style's first. A style that keeps two to four alternatives (another fold operator, primitive or iteration
// rule) crossfades or steps between them along 0..1 as uFormula, so the music can change the shader's formula as
// well as its shape (owner: "music changes shape but also shader formula"). A structural target like Morph:
// LfoTarget::Formula, no luminance limit. It reaches the styles listed in Catalogue.cpp's kFormulaStyles
// (Scope::Formula), none until a style reads uFormula; at 0 every style draws its first formula, as before.
GEODE_PARAM(Float, formula, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM_META(formula, "Formula", Formula, kMod, 0, 0)

#ifdef GEODE_PARAM_TABLE_OWN_PARAM
#undef GEODE_PARAM
#undef GEODE_PARAM_TABLE_OWN_PARAM
#endif
#ifdef GEODE_PARAM_TABLE_OWN_META
#undef GEODE_PARAM_META
#undef GEODE_PARAM_TABLE_OWN_META
#endif
#ifdef GEODE_PARAM_TABLE_OWN_CHOICES
#undef GEODE_PARAM_CHOICES
#undef GEODE_PARAM_TABLE_OWN_CHOICES
#endif
