package dev.geode.editor

import dev.geode.R
import dev.geode.export.ClipEdit
import dev.geode.render.scene.CymaticsMath
import dev.geode.render.scene.MarchBudget
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.SceneParams
import kotlin.math.roundToInt

/** A parameter a keyframe track may drive: where it lives, what shape it has and the range the UI offers. */
data class AnimatableParam(
    val id: ParamId,
    val kind: ParamKind,
    val label: String = "",
    val labelRes: Int = 0,
    val min: Float = 0f,
    val max: Float = 1f,
    val choices: List<String> = emptyList(),
    val readScene: ((SceneParams) -> ParamValue)? = null,
    val writeScene: ((SceneParams, ParamValue) -> SceneParams)? = null,
    val readClip: ((ClipEdit) -> ParamValue)? = null,
    val writeClip: ((ClipEdit, ParamValue) -> ClipEdit)? = null,
) {
    val isScene: Boolean get() = writeScene != null
}

/** The catalogue: scene parameters under `scene.<field>`, clip grade parameters under `clip.<field>`. */
object AnimatableParams {
    private const val SCENE = "scene."
    private const val CLIP = "clip."

    val scene: List<AnimatableParam> = sceneTable()
    val clip: List<AnimatableParam> = clipTable()

    private val byId: Map<ParamId, AnimatableParam> = (scene + clip).associateBy { it.id }

    fun find(id: ParamId): AnimatableParam? = byId[id]

    /** Folds the animated values into a frame's scene parameters; unknown ids are ignored. */
    fun applyToScene(
        base: SceneParams,
        values: Map<ParamId, ParamValue>,
    ): SceneParams =
        values.entries.fold(base) { params, (id, value) ->
            byId[id]?.writeScene?.invoke(params, value) ?: params
        }

    fun applyToClip(
        base: ClipEdit,
        values: Map<ParamId, ParamValue>,
    ): ClipEdit =
        values.entries.fold(base) { edit, (id, value) ->
            byId[id]?.writeClip?.invoke(edit, value) ?: edit
        }

    private fun scalar(
        field: String,
        label: String,
        min: Float,
        max: Float,
        read: (SceneParams) -> Float,
        write: (SceneParams, Float) -> SceneParams,
    ) = AnimatableParam(
        id = ParamId(SCENE + field),
        kind = ParamKind.SCALAR,
        label = label,
        min = min,
        max = max,
        readScene = { ParamValue.Scalar(read(it)) },
        writeScene = { p, v -> if (v is ParamValue.Scalar) write(p, v.value.coerceIn(min, max)) else p },
    )

    private fun count(
        field: String,
        label: String,
        min: Int,
        max: Int,
        read: (SceneParams) -> Int,
        write: (SceneParams, Int) -> SceneParams,
    ) = scalar(field, label, min.toFloat(), max.toFloat(), { read(it).toFloat() }) { p, v -> write(p, v.roundToInt()) }

    private fun toggle(
        field: String,
        label: String,
        read: (SceneParams) -> Boolean,
        write: (SceneParams, Boolean) -> SceneParams,
    ) = AnimatableParam(
        id = ParamId(SCENE + field),
        kind = ParamKind.TOGGLE,
        label = label,
        readScene = { ParamValue.Toggle(read(it)) },
        writeScene = { p, v -> if (v is ParamValue.Toggle) write(p, v.on) else p },
    )

    private fun choice(
        field: String,
        label: String,
        choices: List<String>,
        read: (SceneParams) -> Int,
        write: (SceneParams, Int) -> SceneParams,
    ) = AnimatableParam(
        id = ParamId(SCENE + field),
        kind = ParamKind.CHOICE,
        label = label,
        choices = choices,
        readScene = { ParamValue.Choice(read(it)) },
        writeScene = { p, v -> if (v is ParamValue.Choice) write(p, v.index.coerceIn(0, choices.lastIndex)) else p },
    )

    private fun clipScalar(
        field: String,
        labelRes: Int,
        min: Float,
        max: Float,
        read: (ClipEdit) -> Float,
        write: (ClipEdit, Float) -> ClipEdit,
    ) = AnimatableParam(
        id = ParamId(CLIP + field),
        kind = ParamKind.SCALAR,
        labelRes = labelRes,
        min = min,
        max = max,
        readClip = { ParamValue.Scalar(read(it)) },
        writeClip = { e, v -> if (v is ParamValue.Scalar) write(e, v.value.coerceIn(min, max)) else e },
    )

    private fun clipTable(): List<AnimatableParam> =
        listOf(
            clipScalar("brightness", R.string.studio_brightness, -1f, 1f, { it.brightness }) { e, v -> e.copy(brightness = v) },
            clipScalar("contrast", R.string.studio_contrast, -1f, 1f, { it.contrast }) { e, v -> e.copy(contrast = v) },
            clipScalar("saturation", R.string.studio_saturation, -100f, 100f, { it.saturation }) { e, v -> e.copy(saturation = v) },
            clipScalar("hueDegrees", R.string.studio_hue_shift, -180f, 180f, { it.hueDegrees }) { e, v -> e.copy(hueDegrees = v) },
            clipScalar("speed", R.string.studio_speed, 0.25f, 4f, { it.speed }) { e, v -> e.copy(speed = v) },
            clipScalar(
                "rotationDegrees",
                R.string.studio_rotate,
                -180f,
                180f,
                { it.rotationDegrees },
            ) { e, v -> e.copy(rotationDegrees = v) },
        )

    // Ranges follow the sliders in CustomizeTabs so an animated value never leaves what the panel offers.
    private fun sceneTable(): List<AnimatableParam> =
        listOf(
            scalar("speed", ParamKeys.SPEED, 0.05f, 4f, { it.speed }) { p, v -> p.copy(speed = v) },
            scalar("zoom", ParamKeys.ZOOM, 0.3f, 3f, { it.zoom }) { p, v -> p.copy(zoom = v) },
            scalar("rotation", ParamKeys.ROTATION, -3f, 3f, { it.rotation }) { p, v -> p.copy(rotation = v) },
            scalar("sway", ParamKeys.SWAY, 0f, 1f, { it.sway }) { p, v -> p.copy(sway = v) },
            scalar("turbulence", ParamKeys.TURBULENCE, 0f, 1.5f, { it.turbulence }) { p, v -> p.copy(turbulence = v) },
            scalar("driftX", ParamKeys.DRIFT_X, -1f, 1f, { it.driftX }) { p, v -> p.copy(driftX = v) },
            scalar("driftY", ParamKeys.DRIFT_Y, -1f, 1f, { it.driftY }) { p, v -> p.copy(driftY = v) },
            scalar("pulse", ParamKeys.BEAT_PULSE, 0f, 1f, { it.pulse }) { p, v -> p.copy(pulse = v) },
            scalar("shake", ParamKeys.BEAT_SHAKE, 0f, 1f, { it.shake }) { p, v -> p.copy(shake = v) },
            toggle("endlessZoom", ParamKeys.ENDLESS_ZOOM, { it.endlessZoom }) { p, v -> p.copy(endlessZoom = v) },
            scalar("endlessZoomSpeed", ParamKeys.DIVE_SPEED, 0.05f, 1.2f, { it.endlessZoomSpeed }) { p, v -> p.copy(endlessZoomSpeed = v) },
            toggle("beamXy", ParamKeys.XY_PLOT, { it.beamXy }) { p, v -> p.copy(beamXy = v) },
            scalar("beamWidth", ParamKeys.BEAM_WIDTH, 0.2f, 4f, { it.beamWidth }) { p, v -> p.copy(beamWidth = v) },
            scalar("beamIntensity", ParamKeys.BEAM_BRIGHTNESS, 0f, 3f, { it.beamIntensity }) { p, v -> p.copy(beamIntensity = v) },
            scalar("beamTail", ParamKeys.BEAM_TAIL, 0f, 1f, { it.beamTail }) { p, v -> p.copy(beamTail = v) },
            scalar("warp", ParamKeys.DOMAIN_WARP, 0f, 1f, { it.warp }) { p, v -> p.copy(warp = v) },
            scalar("ripple", ParamKeys.RIPPLE, 0f, 1f, { it.ripple }) { p, v -> p.copy(ripple = v) },
            scalar("morph", ParamKeys.MORPH, 0f, 1f, { it.morph }) { p, v -> p.copy(morph = v) },
            scalar("marchDetail", ParamKeys.MARCH_DETAIL, MarchBudget.MIN_DETAIL, MarchBudget.MAX_DETAIL, {
                it.marchDetail
            }) { p, v -> p.copy(marchDetail = v) },
            scalar("twist", ParamKeys.TWIST, -1f, 1f, { it.twist }) { p, v -> p.copy(twist = v) },
            toggle("kaleidoscope", ParamKeys.KALEIDOSCOPE, { it.kaleidoscope }) { p, v -> p.copy(kaleidoscope = v) },
            toggle("mirror", ParamKeys.MIRROR, { it.mirror }) { p, v -> p.copy(mirror = v) },
            scalar("tile", ParamKeys.TILE, 1f, 6f, { it.tile }) { p, v -> p.copy(tile = v) },
            scalar("pixelate", ParamKeys.PIXELATE, 0f, 1f, { it.pixelate }) { p, v -> p.copy(pixelate = v) },
            choice(
                "particleShape",
                ParamKeys.PARTICLE_SHAPE,
                SceneParams.PARTICLE_SHAPES,
                { it.particleShape },
            ) { p, v -> p.copy(particleShape = v) },
            scalar("particleSize", ParamKeys.PARTICLE_SIZE, 0.3f, 2.5f, { it.particleSize }) { p, v -> p.copy(particleSize = v) },
            scalar("density", ParamKeys.DENSITY, 0.1f, 1f, { it.density }) { p, v -> p.copy(density = v) },
            toggle(
                "milkdropBlendPresets",
                ParamKeys.BLEND_PRESET_CHANGES,
                { it.milkdropBlendPresets },
            ) { p, v -> p.copy(milkdropBlendPresets = v) },
            scalar("audioDrive", ParamKeys.AUDIO_DRIVE, 0.2f, 2.5f, { it.audioDrive }) { p, v -> p.copy(audioDrive = v) },
            scalar("beatResponse", ParamKeys.BEAT_RESPONSE, 0f, 2f, { it.beatResponse }) { p, v -> p.copy(beatResponse = v) },
            scalar("flash", ParamKeys.BEAT_FLASH, 0f, 1f, { it.flash }) { p, v -> p.copy(flash = v) },
            scalar("bassGain", ParamKeys.BASS_GAIN, 0f, 2f, { it.bassGain }) { p, v -> p.copy(bassGain = v) },
            scalar("midGain", ParamKeys.MID_GAIN, 0f, 2f, { it.midGain }) { p, v -> p.copy(midGain = v) },
            scalar("trebGain", ParamKeys.TREBLE_GAIN, 0f, 2f, { it.trebGain }) { p, v -> p.copy(trebGain = v) },
            scalar("paletteMix", ParamKeys.PALETTE_BLEND, 0f, 1f, { it.paletteMix }) { p, v -> p.copy(paletteMix = v) },
            scalar("milkdropPaletteTint", ParamKeys.MILKDROP_PALETTE_TINT, 0f, 1f, {
                it.milkdropPaletteTint
            }) { p, v -> p.copy(milkdropPaletteTint = v) },
            scalar("colorShift", ParamKeys.HUE_SHIFT, 0f, 1f, { it.colorShift }) { p, v -> p.copy(colorShift = v) },
            scalar("hueRange", ParamKeys.HUE_RANGE, 0f, 1.5f, { it.hueRange }) { p, v -> p.copy(hueRange = v) },
            toggle("colorCycle", ParamKeys.COLOR_CYCLE, { it.colorCycle }) { p, v -> p.copy(colorCycle = v) },
            scalar("cycleSpeed", ParamKeys.CYCLE_SPEED, 0.02f, 0.6f, { it.cycleSpeed }) { p, v -> p.copy(cycleSpeed = v) },
            scalar("saturation", ParamKeys.SATURATION, 0f, 1.5f, { it.saturation }) { p, v -> p.copy(saturation = v) },
            scalar("brightness", ParamKeys.BRIGHTNESS, 0.2f, 2f, { it.brightness }) { p, v -> p.copy(brightness = v) },
            scalar("contrast", ParamKeys.CONTRAST, 0.3f, 2.5f, { it.contrast }) { p, v -> p.copy(contrast = v) },
            scalar("gamma", ParamKeys.GAMMA, 0.3f, 2.5f, { it.gamma }) { p, v -> p.copy(gamma = v) },
            scalar("intensity", ParamKeys.INTENSITY, 0.2f, 2f, { it.intensity }) { p, v -> p.copy(intensity = v) },
            scalar("temperature", ParamKeys.TEMPERATURE, -1f, 1f, { it.temperature }) { p, v -> p.copy(temperature = v) },
            scalar("bloom", ParamKeys.BLOOM, 0f, 1f, { it.bloom }) { p, v -> p.copy(bloom = v) },
            scalar("posterize", ParamKeys.POSTERIZE, 0f, 1f, { it.posterize }) { p, v -> p.copy(posterize = v) },
            toggle("duotone", ParamKeys.DUOTONE, { it.duotone }) { p, v -> p.copy(duotone = v) },
            toggle("solarize", ParamKeys.SOLARIZE, { it.solarize }) { p, v -> p.copy(solarize = v) },
            toggle("invert", ParamKeys.INVERT, { it.invert }) { p, v -> p.copy(invert = v) },
            toggle("trails", ParamKeys.TRAILS, { it.trails }) { p, v -> p.copy(trails = v) },
            scalar("trailLength", ParamKeys.TRAIL_LENGTH, 0.05f, 0.98f, { it.trailLength }) { p, v -> p.copy(trailLength = v) },
            scalar("trailZoom", ParamKeys.TRAIL_ZOOM_ECHO_IN_OUT, -0.5f, 0.5f, { it.trailZoom }) { p, v -> p.copy(trailZoom = v) },
            scalar("trailWarp", ParamKeys.TRAIL_WARP_LIQUID_ECHO, 0f, 1f, { it.trailWarp }) { p, v -> p.copy(trailWarp = v) },
            scalar("chromaAb", ParamKeys.CHROMATIC_ABERRATION, 0f, 1f, { it.chromaAb }) { p, v -> p.copy(chromaAb = v) },
            scalar("vignette", ParamKeys.VIGNETTE, 0f, 1f, { it.vignette }) { p, v -> p.copy(vignette = v) },
            scalar("scanlines", ParamKeys.SCANLINES, 0f, 1f, { it.scanlines }) { p, v -> p.copy(scanlines = v) },
            scalar("grain", ParamKeys.FILM_GRAIN, 0f, 1f, { it.grain }) { p, v -> p.copy(grain = v) },
            scalar("glitch", ParamKeys.GLITCH, 0f, 1f, { it.glitch }) { p, v -> p.copy(glitch = v) },
            scalar("fisheye", ParamKeys.FISHEYE, -1f, 1f, { it.fisheye }) { p, v -> p.copy(fisheye = v) },
            scalar("strobe", ParamKeys.STROBE, 0f, 1f, { it.strobe }) { p, v -> p.copy(strobe = v) },
            scalar(
                "waterRippleStrength",
                ParamKeys.RIPPLE_STRENGTH,
                0f,
                2f,
                { it.waterRippleStrength },
            ) { p, v -> p.copy(waterRippleStrength = v) },
            scalar("waterDepth", ParamKeys.DEPTH, 0f, 1f, { it.waterDepth }) { p, v -> p.copy(waterDepth = v) },
            scalar("waterSpecular", ParamKeys.SPECULAR, 0f, 1f, { it.waterSpecular }) { p, v -> p.copy(waterSpecular = v) },
            scalar("waterFlow", ParamKeys.FLOW_DRIFT, 0f, 1f, { it.waterFlow }) { p, v -> p.copy(waterFlow = v) },
            scalar("waterWaveSpeed", ParamKeys.WAVE_SPEED, 0.2f, 2f, { it.waterWaveSpeed }) { p, v -> p.copy(waterWaveSpeed = v) },
            scalar("waterDamping", ParamKeys.DAMPING, 0.9f, 0.999f, { it.waterDamping }) { p, v -> p.copy(waterDamping = v) },
            scalar("waterLiquid", ParamKeys.LIQUID, 0f, 1f, { it.waterLiquid }) { p, v -> p.copy(waterLiquid = v) },
            scalar("waterLiquidFlow", ParamKeys.LIQUID_FLOW, 0f, 4f, { it.waterLiquidFlow }) { p, v -> p.copy(waterLiquidFlow = v) },
            scalar("waterLiquidFade", ParamKeys.LIQUID_FADE, 0f, 2f, { it.waterLiquidFade }) { p, v -> p.copy(waterLiquidFade = v) },
            choice("fluidSpawnPath", ParamKeys.PATH, SceneParams.FLUID_PATHS, { it.fluidSpawnPath }) { p, v -> p.copy(fluidSpawnPath = v) },
            count("fluidSpawnPoints", ParamKeys.SPAWN_POINTS, 1, 8, { it.fluidSpawnPoints }) { p, v -> p.copy(fluidSpawnPoints = v) },
            count("fluidCatchPoints", ParamKeys.CATCH_POINTS, 0, 4, { it.fluidCatchPoints }) { p, v -> p.copy(fluidCatchPoints = v) },
            scalar("fluidCatchPull", ParamKeys.CATCH_PULL, 0f, 3f, { it.fluidCatchPull }) { p, v -> p.copy(fluidCatchPull = v) },
            scalar(
                "fluidCatchRadius",
                ParamKeys.CATCH_RADIUS,
                0.03f,
                0.3f,
                { it.fluidCatchRadius },
            ) { p, v -> p.copy(fluidCatchRadius = v) },
            count("fluidIterations", ParamKeys.SOLVER_ITERATIONS, 8, 40, { it.fluidIterations }) { p, v -> p.copy(fluidIterations = v) },
            scalar("fluidCurl", ParamKeys.FLUID_CURL, 0f, 50f, { it.fluidCurl }) { p, v -> p.copy(fluidCurl = v) },
            scalar("fluidVelocityDissipation", ParamKeys.MOTION_FADE, 0f, 4f, {
                it.fluidVelocityDissipation
            }) { p, v -> p.copy(fluidVelocityDissipation = v) },
            scalar("fluidDensityDissipation", ParamKeys.FLUID_FADE, 0f, 4f, {
                it.fluidDensityDissipation
            }) { p, v -> p.copy(fluidDensityDissipation = v) },
            scalar(
                "fluidChromaticAging",
                ParamKeys.CHROMATIC_AGING,
                0f,
                1f,
                { it.fluidChromaticAging },
            ) { p, v -> p.copy(fluidChromaticAging = v) },
            scalar("fluidPressure", ParamKeys.PRESSURE, 0f, 1f, { it.fluidPressure }) { p, v -> p.copy(fluidPressure = v) },
            choice("fluidBeatPattern", ParamKeys.BEAT_PATTERN, SceneParams.FLUID_PATTERNS, {
                it.fluidBeatPattern
            }) { p, v -> p.copy(fluidBeatPattern = v) },
            count("fluidBeatSplats", ParamKeys.BEAT_SPLATS, 0, 8, { it.fluidBeatSplats }) { p, v -> p.copy(fluidBeatSplats = v) },
            count("fluidStirrers", ParamKeys.STIRRERS, 0, 4, { it.fluidStirrers }) { p, v -> p.copy(fluidStirrers = v) },
            scalar(
                "fluidStirrerSpeed",
                ParamKeys.STIRRER_SPEED,
                0f,
                2f,
                { it.fluidStirrerSpeed },
            ) { p, v -> p.copy(fluidStirrerSpeed = v) },
            scalar(
                "fluidSplatRadius",
                ParamKeys.FLUID_SPLAT_RADIUS,
                0.02f,
                0.4f,
                { it.fluidSplatRadius },
            ) { p, v -> p.copy(fluidSplatRadius = v) },
            scalar("fluidRadiusPulse", ParamKeys.RADIUS_ON_BEAT, 0f, 1f, { it.fluidRadiusPulse }) { p, v -> p.copy(fluidRadiusPulse = v) },
            scalar("fluidSplatForce", ParamKeys.FLUID_SPLAT_FORCE, 0f, 3f, { it.fluidSplatForce }) { p, v -> p.copy(fluidSplatForce = v) },
            toggle("fluidBassPump", ParamKeys.BASS_PUMP, { it.fluidBassPump }) { p, v -> p.copy(fluidBassPump = v) },
            toggle("fluidSparkle", ParamKeys.TREBLE_SPARKLE, { it.fluidSparkle }) { p, v -> p.copy(fluidSparkle = v) },
            scalar("fluidPaletteCycleSpeed", ParamKeys.PALETTE_CYCLE, 0f, 2f, {
                it.fluidPaletteCycleSpeed
            }) { p, v -> p.copy(fluidPaletteCycleSpeed = v) },
            scalar(
                "fluidParticleDrag",
                ParamKeys.PARTICLE_DRAG,
                0.02f,
                1f,
                { it.fluidParticleDrag },
            ) { p, v -> p.copy(fluidParticleDrag = v) },
            scalar(
                "fluidParticleLife",
                ParamKeys.PARTICLE_LIFE_S,
                1f,
                20f,
                { it.fluidParticleLife },
            ) { p, v -> p.copy(fluidParticleLife = v) },
            scalar("fluidParticleBrightness", ParamKeys.PARTICLE_BRIGHTNESS, 0f, 2f, {
                it.fluidParticleBrightness
            }) { p, v -> p.copy(fluidParticleBrightness = v) },
            toggle("fluidShading", ParamKeys.SHADING_EMBOSSED_INK, { it.fluidShading }) { p, v -> p.copy(fluidShading = v) },
            toggle("fluidBloom", ParamKeys.GLOW_FLUID, { it.fluidBloom }) { p, v -> p.copy(fluidBloom = v) },
            scalar(
                "fluidBloomIntensity",
                ParamKeys.FLUID_GLOW,
                0.1f,
                2f,
                { it.fluidBloomIntensity },
            ) { p, v -> p.copy(fluidBloomIntensity = v) },
            scalar(
                "fluidBloomThreshold",
                ParamKeys.GLOW_THRESHOLD,
                0f,
                1f,
                { it.fluidBloomThreshold },
            ) { p, v -> p.copy(fluidBloomThreshold = v) },
            toggle("fluidSunrays", ParamKeys.SUNRAYS, { it.fluidSunrays }) { p, v -> p.copy(fluidSunrays = v) },
            scalar(
                "fluidSunraysWeight",
                ParamKeys.SUNRAYS_WEIGHT,
                0.3f,
                1f,
                { it.fluidSunraysWeight },
            ) { p, v -> p.copy(fluidSunraysWeight = v) },
            scalar("fluidCurlAudio", ParamKeys.CURL_FROM_MIDS, 0f, 1f, { it.fluidCurlAudio }) { p, v -> p.copy(fluidCurlAudio = v) },
            scalar("fluidBloomAudio", ParamKeys.GLOW_FROM_LOUDNESS, 0f, 1f, { it.fluidBloomAudio }) { p, v -> p.copy(fluidBloomAudio = v) },
            scalar("fluidFadeAudio", ParamKeys.FADE_WHEN_QUIET, 0f, 1f, { it.fluidFadeAudio }) { p, v -> p.copy(fluidFadeAudio = v) },
            scalar("flowStrength", ParamKeys.FLOW_STRENGTH, 0f, 1f, { it.flowStrength }) { p, v -> p.copy(flowStrength = v) },
            scalar("flowForce", ParamKeys.FLOW_FORCE, 0f, 3f, { it.flowForce }) { p, v -> p.copy(flowForce = v) },
            scalar("flowCurl", ParamKeys.FLOW_CURL, 0f, 50f, { it.flowCurl }) { p, v -> p.copy(flowCurl = v) },
            scalar("rippleOverlayStrength", ParamKeys.RIPPLE_OVERLAY_STRENGTH, 0f, 1f, {
                it.rippleOverlayStrength
            }) { p, v -> p.copy(rippleOverlayStrength = v) },
            scalar(
                "rippleOverlaySpecular",
                ParamKeys.RIPPLE_GLINT,
                0f,
                1f,
                { it.rippleOverlaySpecular },
            ) { p, v -> p.copy(rippleOverlaySpecular = v) },
            choice("cymaticsGeometry", ParamKeys.GEOMETRY, SceneParams.CYMATICS_GEOMETRIES, {
                it.cymaticsGeometry
            }) { p, v -> p.copy(cymaticsGeometry = v) },
            scalar("cymaticsFundamental", ParamKeys.FUNDAMENTAL_HZ, CymaticsMath.MIN_FUNDAMENTAL_HZ, CymaticsMath.MAX_FUNDAMENTAL_HZ, {
                it.cymaticsFundamental
            }) { p, v -> p.copy(cymaticsFundamental = v) },
            count("cymaticsModes", ParamKeys.STANDING_WAVES, 1, CymaticsMath.MAX_RENDERED_MODES, {
                it.cymaticsModes
            }) { p, v -> p.copy(cymaticsModes = v) },
            scalar("cymaticsFocus", ParamKeys.TONAL_FOCUS, 0f, 1f, { it.cymaticsFocus }) { p, v -> p.copy(cymaticsFocus = v) },
            scalar("cymaticsRing", ParamKeys.PLATE_RING, 0f, 1f, { it.cymaticsRing }) { p, v -> p.copy(cymaticsRing = v) },
            scalar("cymaticsScale", ParamKeys.FIELD_SCALE, 0.5f, 8f, { it.cymaticsScale }) { p, v -> p.copy(cymaticsScale = v) },
            scalar("cymaticsFlow", ParamKeys.WAVE_FLOW, 0f, 1f, { it.cymaticsFlow }) { p, v -> p.copy(cymaticsFlow = v) },
            scalar("cymaticsSwirl", ParamKeys.FIELD_SWIRL, -1f, 1f, { it.cymaticsSwirl }) { p, v -> p.copy(cymaticsSwirl = v) },
            scalar("cymaticsFill", ParamKeys.FILL, 0f, 1f, { it.cymaticsFill }) { p, v -> p.copy(cymaticsFill = v) },
            scalar("cymaticsLine", ParamKeys.NODAL_LINES, 0f, 2f, { it.cymaticsLine }) { p, v -> p.copy(cymaticsLine = v) },
            scalar("cymaticsGlow", ParamKeys.NODAL_GLOW, 0f, 2f, { it.cymaticsGlow }) { p, v -> p.copy(cymaticsGlow = v) },
            scalar(
                "cymaticsIridescence",
                ParamKeys.IRIDESCENCE,
                0f,
                1f,
                { it.cymaticsIridescence },
            ) { p, v -> p.copy(cymaticsIridescence = v) },
            scalar("cymaticsCaustic", ParamKeys.CAUSTIC_SHEEN, 0f, 1.5f, { it.cymaticsCaustic }) { p, v -> p.copy(cymaticsCaustic = v) },
        )
}
