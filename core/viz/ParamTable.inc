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
// The row order IS the geode_viz_set_params wire: row i is wire slot i, and SceneParamsCodec packs by
// these rows (it reads them through ParamSchema; WireLayoutTest holds the Kotlin fields to them, and
// params_schema_test holds core/tests/params_schema.txt, the JVM tests' copy, to the text). Never insert or reorder:
// append, and retire a field by leaving its row where it is. core/tests/params_schema_test.cpp checks
// the table; geode_params_schema_text publishes it to the shell.

GEODE_PARAM(Float, speed, 1.0f, 0.05f, 4.0f, Fade)
// A pinch scales Zoom and a twist turns Rotation, held to the catalogue's touch ranges.
GEODE_PARAM(Float, zoom, 1.0f, catalogue::kTouchZoom.min, catalogue::kTouchZoom.max, Fade)
GEODE_PARAM(Float, rotation, 0.0f, catalogue::kTouchRotation.min, catalogue::kTouchRotation.max, Fade)
GEODE_PARAM(Bool, endlessZoom, false, 0, 1, Hold)
GEODE_PARAM(Float, endlessZoomSpeed, 0.3f, -1.2f, 1.2f, Fade)
GEODE_PARAM(Float, sway, 0.0f, 0.0f, 1.0f, Fade)
// Inert since wave three; removed with the next format bump.
GEODE_PARAM(Float, pulse, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, driftX, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM(Float, driftY, 0.0f, -1.0f, 1.0f, Fade)
// Inert since wave three; removed with the next format bump.
GEODE_PARAM(Float, shake, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, audioDrive, 1.0f, 0.2f, 2.5f, Fade)
// Inert since wave three; removed with the next format bump.
GEODE_PARAM(Float, beatResponse, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM(Float, turbulence, 0.0f, 0.0f, 1.5f, Fade)
GEODE_PARAM(Float, density, 1.0f, 0.1f, 1.0f, Fade)
// The user's Detail control; ShaderScene turns it into the march step budget.
GEODE_PARAM(Float, marchDetail, 1.0f, catalogue::kMarchDetail.min, catalogue::kMarchDetail.max, Fade)
GEODE_PARAM(Bool, trails, true, 0, 1, Hold)
GEODE_PARAM(Float, trailLength, 0.5f, 0.05f, 0.98f, Fade)
GEODE_PARAM(Float, trailZoom, 0.0f, -0.5f, 0.5f, Fade)
GEODE_PARAM(Float, trailWarp, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Bool, mirror, false, 0, 1, Hold)
GEODE_PARAM(Float, warp, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, ripple, 0.0f, 0.0f, 1.0f, Fade)
// Kaleidoscope folds: SceneParams.SYMMETRY_FOLDS (0, 2..9, 12, 16).
GEODE_PARAM(Int, symmetry, SceneParams::kDefaultSymmetryFolds, 0, 16, Hold)
GEODE_PARAM(Bool, kaleidoscope, false, 0, 1, Hold)
GEODE_PARAM(Float, morph, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, pixelate, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, posterize, 0.0f, 0.0f, 1.0f, Fade)
// Seven sprite shapes: Dot, Ring, Star, Square, Spark, Hex, Bubble.
GEODE_PARAM(Int, particleShape, 0, 0, 6, Hold)
GEODE_PARAM(Float, particleSize, 1.0f, 0.3f, 2.5f, Fade)
GEODE_PARAM(Float, tile, 1.0f, 1.0f, 6.0f, Fade)
GEODE_PARAM(Float, twist, 0.0f, -1.0f, 1.0f, Fade)
// An index into SceneParams::palettes().
GEODE_PARAM(Int, palette, 0, 0, static_cast<float>(SceneParams::palettes().size() - 1), Hold)
GEODE_PARAM(Int, palette2, 1, 0, static_cast<float>(SceneParams::palettes().size() - 1), Hold)
GEODE_PARAM(Float, paletteMix, 0.0f, 0.0f, 1.0f, Fade)
// A custom palette's hue base and span, 0..1; kUnsetOverride (-1) = use the palette's own. Never
// faded: a glide through the sentinel would flash the palette's own colours mid-fade.
GEODE_PARAM(Float, paletteBaseOverride, SceneParams::kUnsetOverride, SceneParams::kUnsetOverride, 1.0f, Hold)
GEODE_PARAM(Float, paletteRangeOverride, SceneParams::kUnsetOverride, SceneParams::kUnsetOverride, 1.0f, Hold)
GEODE_PARAM(Float, palette2BaseOverride, SceneParams::kUnsetOverride, SceneParams::kUnsetOverride, 1.0f, Hold)
GEODE_PARAM(Float, palette2RangeOverride, SceneParams::kUnsetOverride, SceneParams::kUnsetOverride, 1.0f, Hold)
// A cyclic colour map row (five: CyclicPalettes.NAMES); kNoPaletteLut (-1) = off.
GEODE_PARAM(Int, paletteLut, SceneParams::kNoPaletteLut, SceneParams::kNoPaletteLut, 4, Hold)
GEODE_PARAM(Float, milkdropPaletteTint, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Bool, milkdropBlendPresets, false, 0, 1, Hold)
// A hue turn: 1 is the full circle, and the LFO adds to it unclamped.
GEODE_PARAM(Circular, colorShift, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, hueRange, 1.0f, 0.0f, 1.5f, Fade)
GEODE_PARAM(Float, saturation, 1.0f, 0.0f, 1.5f, Fade)
// The slider and keyframes stop at 1.25 (1 + the flash clamp's depth); the LFO and the routes reach 2.
GEODE_PARAM(Float, brightness, 1.0f, 0.2f, 2.0f, Fade)
GEODE_PARAM(Float, contrast, 1.0f, 0.3f, 1.25f, Fade)
GEODE_PARAM(Float, gamma, 1.0f, 0.3f, 2.5f, Fade)
GEODE_PARAM(Bool, colorCycle, false, 0, 1, Hold)
GEODE_PARAM(Float, cycleSpeed, 0.1f, 0.02f, 0.6f, Fade)
GEODE_PARAM(Bool, invert, false, 0, 1, Hold)
// As brightness: the slider stops at 1.25, the LFO reaches 2.
GEODE_PARAM(Float, intensity, 1.0f, 0.2f, 2.0f, Fade)
GEODE_PARAM(Bool, duotone, false, 0, 1, Hold)
// The slider and keyframes stop at 0.25 (the flash clamp's depth); the LFO and the routes reach 1.
GEODE_PARAM(Float, bloom, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, temperature, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM(Bool, solarize, false, 0, 1, Hold)
GEODE_PARAM(Float, bassGain, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM(Float, midGain, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM(Float, trebGain, 1.0f, 0.0f, 2.0f, Fade)
// Inert since wave three; removed with the next format bump.
GEODE_PARAM(Float, flash, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, chromaAb, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, vignette, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, scanlines, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, grain, 0.0f, 0.0f, 1.0f, Fade)
// Retired: the composite Glitch effect is gone. No reader; removed with the next format bump.
GEODE_PARAM(Float, glitch, 0.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, fisheye, 0.0f, -1.0f, 1.0f, Fade)
// Inert since wave three; removed with the next format bump.
GEODE_PARAM(Float, strobe, 0.0f, 0.0f, 1.0f, Fade)
// Customize › Advanced › Glide between settings, in seconds: the fade's own length, never faded.
GEODE_PARAM(Float, paramFadeSec, 0.0f, 0.0f, 5.0f, Hold)
// A fluid::quality tier index, 0 Ultra .. Min.
GEODE_PARAM(Int, fluidQuality, 2, 0, static_cast<float>(fluid::quality::kTierCount - 1), Hold)
GEODE_PARAM(Bool, fluidAutoQuality, true, 0, 1, Hold)
GEODE_PARAM(Int, fluidIterations, 20, 8, 40, Hold)
GEODE_PARAM(Float, fluidPressure, 0.8f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, fluidCurl, 30.0f, 0.0f, 50.0f, Fade)
GEODE_PARAM(Float, fluidVelocityDissipation, 0.2f, 0.0f, 4.0f, Fade)
GEODE_PARAM(Float, fluidDensityDissipation, 1.0f, 0.0f, 4.0f, Fade)
GEODE_PARAM(Float, fluidChromaticAging, 0.3f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, fluidSplatRadius, 0.12f, 0.02f, 0.4f, Fade)
GEODE_PARAM(Float, fluidSplatForce, 1.0f, 0.0f, 3.0f, Fade)
// Center, Ring, Random, Spectrum (FluidEmitters clamps to the same).
GEODE_PARAM(Int, fluidBeatPattern, 1, 0, 3, Hold)
GEODE_PARAM(Int, fluidBeatSplats, 3, 0, 8, Hold)
GEODE_PARAM(Int, fluidStirrers, 2, 0, 4, Hold)
GEODE_PARAM(Float, fluidStirrerSpeed, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM(Bool, fluidBassPump, false, 0, 1, Hold)
GEODE_PARAM(Float, fluidPaletteCycleSpeed, 0.5f, 0.0f, 2.0f, Fade)
GEODE_PARAM(Bool, fluidSparkle, true, 0, 1, Hold)
// Orbit, Lissajous, Rose, Bloom, Drift (fluid::Choreography::kPathCount).
GEODE_PARAM(Int, fluidSpawnPath, 1, 0, 4, Hold)
GEODE_PARAM(Int, fluidSpawnPoints, 3, 1, 8, Hold)
GEODE_PARAM(Float, fluidSpawnProgress, 1.0f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Int, fluidCatchPoints, 2, 0, 4, Hold)
GEODE_PARAM(Float, fluidCatchPull, 1.0f, 0.0f, 3.0f, Fade)
GEODE_PARAM(Float, fluidCatchRadius, 0.12f, 0.03f, 0.3f, Fade)
GEODE_PARAM(Bool, fluidParticlesEnabled, true, 0, 1, Hold)
GEODE_PARAM(Float, fluidParticleLife, 6.0f, 1.0f, 20.0f, Fade)
GEODE_PARAM(Float, fluidParticleDrag, 0.5f, 0.02f, 1.0f, Fade)
GEODE_PARAM(Float, fluidParticleBrightness, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM(Bool, fluidDyeEnabled, true, 0, 1, Hold)
GEODE_PARAM(Bool, fluidShading, true, 0, 1, Hold)
GEODE_PARAM(Bool, fluidBloom, true, 0, 1, Hold)
GEODE_PARAM(Float, fluidBloomIntensity, 0.8f, 0.1f, 2.0f, Fade)
GEODE_PARAM(Float, fluidBloomThreshold, 0.6f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Bool, fluidSunrays, true, 0, 1, Hold)
GEODE_PARAM(Float, fluidSunraysWeight, 1.0f, 0.3f, 1.0f, Fade)
GEODE_PARAM(Float, fluidCurlAudio, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, fluidBloomAudio, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, fluidFadeAudio, 0.6f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, fluidRadiusPulse, 0.4f, 0.0f, 1.0f, Fade)
// Liquid mode (viz/liquid; Customize › Advanced › Liquid, Pro): the switch is flowEnabled, Liquidity is
// flowStrength, Push is flowForce and Swirl is flowCurl, the old flow field's fields, so a preset saved
// with "Flow field on" opens as a liquid.
GEODE_PARAM(Bool, flowEnabled, false, 0, 1, Hold)
GEODE_PARAM(Float, flowStrength, 0.35f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, flowForce, 1.0f, 0.0f, 3.0f, Fade)
GEODE_PARAM(Float, flowCurl, 25.0f, 0.0f, 50.0f, Fade)
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
GEODE_PARAM(Float, cymaticsFundamental, 110.0f, cymatics::kMinFundamentalHz, cymatics::kMaxFundamentalHz, Fade)
GEODE_PARAM(Int, cymaticsModes, 5, 1, static_cast<float>(cymatics::kMaxRenderedModes), Hold)
GEODE_PARAM(Float, cymaticsRing, 0.35f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, cymaticsFocus, 0.7f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, cymaticsScale, 3.2f, 0.5f, 8.0f, Fade)
GEODE_PARAM(Float, cymaticsFill, 0.45f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, cymaticsLine, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM(Float, cymaticsGlow, 1.0f, 0.0f, 2.0f, Fade)
GEODE_PARAM(Float, cymaticsIridescence, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, cymaticsCaustic, 0.8f, 0.0f, 1.5f, Fade)
GEODE_PARAM(Float, cymaticsFlow, 0.35f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, cymaticsSwirl, 0.05f, -1.0f, 1.0f, Fade)
GEODE_PARAM(Bool, rippleOverlayEnabled, false, 0, 1, Hold)
GEODE_PARAM(Float, rippleOverlayStrength, 0.4f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, rippleOverlaySpecular, 0.3f, 0.0f, 1.0f, Fade)
// How far the superformula driver (viz/FormDrive) moved every family's parameters with the music.
// Inert since wave three (viz/MotionField replaced it); kept, and 0 still switches nothing on, so a
// preset saved before wave three still decodes.
GEODE_PARAM(Float, formDrive, 0.7f, 0.0f, 1.0f, Fade)

// Wave three: the continuous motion system (viz/MotionField), which replaces formDrive, beatResponse,
// flash, strobe, pulse and shake as the way the music moves a scene. Appended after the older fields so
// every existing wire index survives.
GEODE_PARAM(Float, motionAmount, 0.7f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, motionBreath, 0.5f, 0.0f, 1.0f, Fade)
// No native reader and no control.
GEODE_PARAM(Float, motionOrbit, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, motionDrift, 0.5f, 0.0f, 1.0f, Fade)
GEODE_PARAM(Float, motionHue, 0.4f, 0.0f, 1.0f, Fade)

// The shared camera rig (viz/CameraRig) and the reaction matrix (viz/Reactions). Appended after wave
// three so every wire index survives. The cam* offsets (push, yaw, pitch, roll) are what reaction
// routes and LFOs write; the rig springs toward them, so a route never snaps the view.
// Travel speed along the path, negative backward: the slider stops at -2..2, the LFO and the routes reach 2.5.
GEODE_PARAM(Float, camFly, 0.5f, -2.5f, 2.5f, Fade)
// 0 glide, 1 spiral, 2 orbit, 3 hover, 4 drive, 5 fall, 6 rise (CameraRig clamps to the same).
GEODE_PARAM(Int, camPath, 0, 0, 6, Hold)
// Sideways swing, or the orbit's radius.
GEODE_PARAM(Float, camOrbit, 0.35f, 0.0f, 1.0f, Fade)
// The dolly offset.
GEODE_PARAM(Float, camPush, 0.0f, -1.0f, 1.0f, Fade)
// The look offsets.
GEODE_PARAM(Float, camYaw, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM(Float, camPitch, 0.0f, -1.0f, 1.0f, Fade)
GEODE_PARAM(Float, camRoll, 0.0f, -1.0f, 1.0f, Fade)
// A field-of-view multiplier.
GEODE_PARAM(Float, camFov, 1.0f, 0.5f, 2.0f, Fade)
// Handheld wobble.
GEODE_PARAM(Float, camShake, 0.0f, 0.0f, 1.0f, Fade)
// Spring softness: 0 tight .. 1 floaty.
GEODE_PARAM(Float, camSmooth, 0.5f, 0.0f, 1.0f, Fade)
// Drag to look around.
GEODE_PARAM(Bool, camTouch, false, 0, 1, Hold)
// Phone tilt steers the view (never in an export).
GEODE_PARAM(Bool, camGyro, false, 0, 1, Hold)
// The global scale of every reaction route.
GEODE_PARAM(Float, reactAmount, 1.0f, 0.0f, 2.0f, Fade)
// Which variant of the style's reaction DNA. It travels as a float, so it stops at 2^24 - 1, the last
// int a float carries exactly.
GEODE_PARAM(Int, reactSeed, 0, 0, 16777215, Hold)

// Note & key reactions (viz/NoteResponse), per style like every Customize control, so a preset, a
// template and an export carry them. Appended so every existing wire index survives. Inert while
// Settings › Behaviour › Note & key reactions is off: the layer then does nothing at all.
// Customize › Music › Notes & key (Pro): how much each part feeds the style knobs, 0 none .. 1 as
// designed .. 2.
// Read by nothing: it was the tint toward the notes' colour, which is gone. Kept so the wire indices
// and saved looks survive.
GEODE_PARAM(Float, noteColor, 1.0f, 0.0f, 2.0f, Fade)
// Pitch height: the TONE knob.
GEODE_PARAM(Float, notePitch, 1.0f, 0.0f, 2.0f, Fade)
// Leaps and steps: the swing and glide behind the SPACE knob.
GEODE_PARAM(Float, noteIntervals, 1.0f, 0.0f, 2.0f, Fade)
// Harmonic tension and builds: the BUILD knob.
GEODE_PARAM(Float, noteTension, 1.0f, 0.0f, 2.0f, Fade)
// A chord change turns the swing behind the SPACE knob.
GEODE_PARAM(Float, noteChords, 1.0f, 0.0f, 2.0f, Fade)
// The drums' push on the PULSE knob, on top of how percussive the track is.
GEODE_PARAM(Float, noteDrums, 1.0f, 0.0f, 2.0f, Fade)

// Liquid mode's pickers (viz/liquid). Appended so every existing wire index survives.
// What forms at the vanishing point: liquid::Born (0 live, 1 re-rolled, 2 next style, 3 ink forms).
GEODE_PARAM(Int, liquidBorn, 1, 0, 3, Hold)
// liquid::Age: 0 pure palette, 1 fade deep, 2 ink bleed.
GEODE_PARAM(Int, liquidAge, 0, 0, 2, Hold)
// liquid::Surface: 0 ink, 1 wet gloss, 2 glass.
GEODE_PARAM(Int, liquidSurface, 0, 0, 2, Hold)

// Camera travel for the flat (2D) styles: how much Fly speed (its sign too) zooms the picture in or
// out without end, 0 off .. 1 (CameraRig::flatDive). Appended so every wire index survives.
GEODE_PARAM(Float, camFlat, 0.0f, 0.0f, 1.0f, Fade)

// Endless zoom's tunnel (viz/EndlessZoom.hpp, tunnel::Look; Customize › Dive). Appended so every wire
// index survives. Each default is the plain tunnel, so a look saved before them dives as it did.
// Octaves between two nested copies.
GEODE_PARAM(Float, diveSpacing, 1.0f, 0.5f, 2.0f, Fade)
// Quarter turns a copy makes per octave it grows: the tunnel as a spiral.
GEODE_PARAM(Float, diveTwist, 0.0f, -1.0f, 1.0f, Fade)
// The vanishing point, -1..1 across the frame.
GEODE_PARAM(Float, diveAimX, 0.0f, -0.6f, 0.6f, Fade)
GEODE_PARAM(Float, diveAimY, 0.0f, -0.6f, 0.6f, Fade)
// A copy's outline: 0 round, 0.5 the rounded frame, 1 nearly square.
GEODE_PARAM(Float, diveShape, 0.5f, 0.0f, 1.0f, Fade)
// How much of a copy's band dissolves into the copy around it: 0 a cut .. 1 the whole band.
GEODE_PARAM(Float, diveBlend, 0.75f, 0.0f, 1.0f, Fade)
// How far the far end of the tunnel falls into dark.
GEODE_PARAM(Float, diveFog, 0.0f, 0.0f, 1.0f, Fade)
// How much more (above 0) or less of the picture each copy's band holds.
GEODE_PARAM(Float, diveLens, 0.0f, -0.5f, 1.5f, Fade)
// The music's slow hand on the dive's rate (endless::Follow): 0 none, 1 as designed, 2 twice the reach.
GEODE_PARAM(Float, diveFollow, 1.0f, 0.0f, 2.0f, Fade)
