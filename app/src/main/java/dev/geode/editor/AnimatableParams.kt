package dev.geode.editor

import dev.geode.R
import dev.geode.export.ClipEdit
import dev.geode.model.ControlRange
import dev.geode.render.scene.ConfigSchema
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

    /**
     * A scene scalar track. Its range is the param's Customize range, [ControlRange.of] by
     * [field], so a keyframe never leaves what the slider offers (#346); a field the schema lacks
     * throws when this table is built.
     */
    private fun scalar(
        field: String,
        label: String,
        read: (SceneParams) -> Float,
        write: (SceneParams, Float) -> SceneParams,
    ): AnimatableParam {
        val range = ControlRange.of(field)
        return AnimatableParam(
            id = ParamId(SCENE + field),
            kind = ParamKind.SCALAR,
            label = label,
            min = range.start,
            max = range.endInclusive,
            readScene = { ParamValue.Scalar(read(it)) },
            writeScene = { p, v -> if (v is ParamValue.Scalar) write(p, v.value.coerceIn(range)) else p },
        )
    }

    /** A whole-number scene track: a [scalar] whose value rounds, over the schema's whole-number range. */
    private fun count(
        field: String,
        label: String,
        read: (SceneParams) -> Int,
        write: (SceneParams, Int) -> SceneParams,
    ) = scalar(field, label, { read(it).toFloat() }) { p, v -> write(p, v.roundToInt()) }

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

    // A clip field's range is its config schema row (core/viz/ConfigTable.inc, group "clip", named as the field).
    private fun clipScalar(
        field: String,
        labelRes: Int,
        read: (ClipEdit) -> Float,
        write: (ClipEdit, Float) -> ClipEdit,
    ): AnimatableParam {
        val range = ConfigSchema["clip", field].range
        return AnimatableParam(
            id = ParamId(CLIP + field),
            kind = ParamKind.SCALAR,
            labelRes = labelRes,
            min = range.start,
            max = range.endInclusive,
            readClip = { ParamValue.Scalar(read(it)) },
            writeClip = { e, v -> if (v is ParamValue.Scalar) write(e, v.value.coerceIn(range)) else e },
        )
    }

    private fun clipTable(): List<AnimatableParam> =
        listOf(
            clipScalar("brightness", R.string.studio_brightness, { it.brightness }) { e, v -> e.copy(brightness = v) },
            clipScalar("contrast", R.string.studio_contrast, { it.contrast }) { e, v -> e.copy(contrast = v) },
            clipScalar("saturation", R.string.studio_saturation, { it.saturation }) { e, v -> e.copy(saturation = v) },
            clipScalar("hueDegrees", R.string.studio_hue_shift, { it.hueDegrees }) { e, v -> e.copy(hueDegrees = v) },
            clipScalar("speed", R.string.studio_speed, { it.speed }) { e, v -> e.copy(speed = v) },
            clipScalar("rotationDegrees", R.string.studio_rotate, { it.rotationDegrees }) { e, v -> e.copy(rotationDegrees = v) },
        )

    // Ranges are the schema's control ranges, the sliders' own, so an animated value never leaves what the panel offers.
    private fun sceneTable(): List<AnimatableParam> =
        listOf(
            scalar("speed", ParamKeys.SPEED, { it.speed }) { p, v -> p.copy(speed = v) },
            scalar("zoom", ParamKeys.ZOOM, { it.zoom }) { p, v -> p.copy(zoom = v) },
            scalar("rotation", ParamKeys.ROTATION, { it.rotation }) { p, v -> p.copy(rotation = v) },
            scalar("sway", ParamKeys.SWAY, { it.sway }) { p, v -> p.copy(sway = v) },
            scalar("turbulence", ParamKeys.TURBULENCE, { it.turbulence }) { p, v -> p.copy(turbulence = v) },
            scalar("driftX", ParamKeys.DRIFT_X, { it.driftX }) { p, v -> p.copy(driftX = v) },
            scalar("driftY", ParamKeys.DRIFT_Y, { it.driftY }) { p, v -> p.copy(driftY = v) },
            scalar("camFly", ParamKeys.CAM_FLY, { it.camFly }) { p, v -> p.copy(camFly = v) },
            scalar("camOrbit", ParamKeys.CAM_ORBIT, { it.camOrbit }) { p, v -> p.copy(camOrbit = v) },
            scalar("camPush", ParamKeys.CAM_PUSH, { it.camPush }) { p, v -> p.copy(camPush = v) },
            scalar("camYaw", ParamKeys.CAM_YAW, { it.camYaw }) { p, v -> p.copy(camYaw = v) },
            scalar("camPitch", ParamKeys.CAM_PITCH, { it.camPitch }) { p, v -> p.copy(camPitch = v) },
            scalar("camRoll", ParamKeys.CAM_ROLL, { it.camRoll }) { p, v -> p.copy(camRoll = v) },
            scalar("camFov", ParamKeys.CAM_FOV, { it.camFov }) { p, v -> p.copy(camFov = v) },
            scalar("camShake", ParamKeys.CAM_SHAKE, { it.camShake }) { p, v -> p.copy(camShake = v) },
            toggle("endlessZoom", ParamKeys.ENDLESS_ZOOM, { it.endlessZoom }) { p, v -> p.copy(endlessZoom = v) },
            scalar("endlessZoomSpeed", ParamKeys.DIVE_SPEED, { it.endlessZoomSpeed }) { p, v -> p.copy(endlessZoomSpeed = v) },
            scalar("warp", ParamKeys.DOMAIN_WARP, { it.warp }) { p, v -> p.copy(warp = v) },
            scalar("ripple", ParamKeys.RIPPLE, { it.ripple }) { p, v -> p.copy(ripple = v) },
            scalar("morph", ParamKeys.MORPH, { it.morph }) { p, v -> p.copy(morph = v) },
            scalar("marchDetail", ParamKeys.MARCH_DETAIL, {
                it.marchDetail
            }) { p, v -> p.copy(marchDetail = v) },
            scalar("twist", ParamKeys.TWIST, { it.twist }) { p, v -> p.copy(twist = v) },
            toggle("kaleidoscope", ParamKeys.KALEIDOSCOPE, { it.kaleidoscope }) { p, v -> p.copy(kaleidoscope = v) },
            toggle("mirror", ParamKeys.MIRROR, { it.mirror }) { p, v -> p.copy(mirror = v) },
            scalar("tile", ParamKeys.TILE, { it.tile }) { p, v -> p.copy(tile = v) },
            scalar("pixelate", ParamKeys.PIXELATE, { it.pixelate }) { p, v -> p.copy(pixelate = v) },
            choice(
                "particleShape",
                ParamKeys.PARTICLE_SHAPE,
                SceneParams.PARTICLE_SHAPES,
                { it.particleShape },
            ) { p, v -> p.copy(particleShape = v) },
            scalar("particleSize", ParamKeys.PARTICLE_SIZE, { it.particleSize }) { p, v -> p.copy(particleSize = v) },
            scalar("density", ParamKeys.DENSITY, { it.density }) { p, v -> p.copy(density = v) },
            toggle(
                "milkdropBlendPresets",
                ParamKeys.BLEND_PRESET_CHANGES,
                { it.milkdropBlendPresets },
            ) { p, v -> p.copy(milkdropBlendPresets = v) },
            scalar("audioDrive", ParamKeys.AUDIO_DRIVE, { it.audioDrive }) { p, v -> p.copy(audioDrive = v) },
            scalar("bassGain", ParamKeys.BASS_GAIN, { it.bassGain }) { p, v -> p.copy(bassGain = v) },
            scalar("midGain", ParamKeys.MID_GAIN, { it.midGain }) { p, v -> p.copy(midGain = v) },
            scalar("trebGain", ParamKeys.TREBLE_GAIN, { it.trebGain }) { p, v -> p.copy(trebGain = v) },
            scalar("paletteMix", ParamKeys.PALETTE_BLEND, { it.paletteMix }) { p, v -> p.copy(paletteMix = v) },
            scalar("milkdropPaletteTint", ParamKeys.MILKDROP_PALETTE_TINT, {
                it.milkdropPaletteTint
            }) { p, v -> p.copy(milkdropPaletteTint = v) },
            scalar("colorShift", ParamKeys.HUE_SHIFT, { it.colorShift }) { p, v -> p.copy(colorShift = v) },
            scalar("hueRange", ParamKeys.HUE_RANGE, { it.hueRange }) { p, v -> p.copy(hueRange = v) },
            toggle("colorCycle", ParamKeys.COLOR_CYCLE, { it.colorCycle }) { p, v -> p.copy(colorCycle = v) },
            scalar("cycleSpeed", ParamKeys.CYCLE_SPEED, { it.cycleSpeed }) { p, v -> p.copy(cycleSpeed = v) },
            scalar("saturation", ParamKeys.SATURATION, { it.saturation }) { p, v -> p.copy(saturation = v) },
            scalar("brightness", ParamKeys.BRIGHTNESS, { it.brightness }) { p, v -> p.copy(brightness = v) },
            scalar("contrast", ParamKeys.CONTRAST, { it.contrast }) { p, v -> p.copy(contrast = v) },
            scalar("gamma", ParamKeys.GAMMA, { it.gamma }) { p, v -> p.copy(gamma = v) },
            scalar("intensity", ParamKeys.INTENSITY, { it.intensity }) { p, v -> p.copy(intensity = v) },
            scalar("temperature", ParamKeys.TEMPERATURE, { it.temperature }) { p, v -> p.copy(temperature = v) },
            scalar("bloom", ParamKeys.BLOOM, { it.bloom }) { p, v -> p.copy(bloom = v) },
            scalar("posterize", ParamKeys.POSTERIZE, { it.posterize }) { p, v -> p.copy(posterize = v) },
            toggle("duotone", ParamKeys.DUOTONE, { it.duotone }) { p, v -> p.copy(duotone = v) },
            toggle("solarize", ParamKeys.SOLARIZE, { it.solarize }) { p, v -> p.copy(solarize = v) },
            toggle("invert", ParamKeys.INVERT, { it.invert }) { p, v -> p.copy(invert = v) },
            toggle("trails", ParamKeys.TRAILS, { it.trails }) { p, v -> p.copy(trails = v) },
            scalar("trailLength", ParamKeys.TRAIL_LENGTH, { it.trailLength }) { p, v -> p.copy(trailLength = v) },
            scalar("trailZoom", ParamKeys.TRAIL_ZOOM_ECHO_IN_OUT, { it.trailZoom }) { p, v -> p.copy(trailZoom = v) },
            scalar("trailWarp", ParamKeys.TRAIL_WARP_LIQUID_ECHO, { it.trailWarp }) { p, v -> p.copy(trailWarp = v) },
            scalar("chromaAb", ParamKeys.CHROMATIC_ABERRATION, { it.chromaAb }) { p, v -> p.copy(chromaAb = v) },
            scalar("vignette", ParamKeys.VIGNETTE, { it.vignette }) { p, v -> p.copy(vignette = v) },
            scalar("scanlines", ParamKeys.SCANLINES, { it.scanlines }) { p, v -> p.copy(scanlines = v) },
            scalar("grain", ParamKeys.FILM_GRAIN, { it.grain }) { p, v -> p.copy(grain = v) },
            scalar("fisheye", ParamKeys.FISHEYE, { it.fisheye }) { p, v -> p.copy(fisheye = v) },
            scalar(
                "waterRippleStrength",
                ParamKeys.RIPPLE_STRENGTH,
                { it.waterRippleStrength },
            ) { p, v -> p.copy(waterRippleStrength = v) },
            scalar("waterDepth", ParamKeys.DEPTH, { it.waterDepth }) { p, v -> p.copy(waterDepth = v) },
            scalar("waterSpecular", ParamKeys.SPECULAR, { it.waterSpecular }) { p, v -> p.copy(waterSpecular = v) },
            scalar("waterFlow", ParamKeys.FLOW_DRIFT, { it.waterFlow }) { p, v -> p.copy(waterFlow = v) },
            scalar("waterWaveSpeed", ParamKeys.WAVE_SPEED, { it.waterWaveSpeed }) { p, v -> p.copy(waterWaveSpeed = v) },
            scalar("waterDamping", ParamKeys.DAMPING, { it.waterDamping }) { p, v -> p.copy(waterDamping = v) },
            scalar("waterLiquid", ParamKeys.LIQUID, { it.waterLiquid }) { p, v -> p.copy(waterLiquid = v) },
            scalar("waterLiquidFlow", ParamKeys.LIQUID_FLOW, { it.waterLiquidFlow }) { p, v -> p.copy(waterLiquidFlow = v) },
            scalar("waterLiquidFade", ParamKeys.LIQUID_FADE, { it.waterLiquidFade }) { p, v -> p.copy(waterLiquidFade = v) },
            choice("fluidSpawnPath", ParamKeys.PATH, SceneParams.FLUID_PATHS, { it.fluidSpawnPath }) { p, v -> p.copy(fluidSpawnPath = v) },
            count("fluidSpawnPoints", ParamKeys.SPAWN_POINTS, { it.fluidSpawnPoints }) { p, v -> p.copy(fluidSpawnPoints = v) },
            count("fluidCatchPoints", ParamKeys.CATCH_POINTS, { it.fluidCatchPoints }) { p, v -> p.copy(fluidCatchPoints = v) },
            scalar("fluidCatchPull", ParamKeys.CATCH_PULL, { it.fluidCatchPull }) { p, v -> p.copy(fluidCatchPull = v) },
            scalar(
                "fluidCatchRadius",
                ParamKeys.CATCH_RADIUS,
                { it.fluidCatchRadius },
            ) { p, v -> p.copy(fluidCatchRadius = v) },
            count("fluidIterations", ParamKeys.SOLVER_ITERATIONS, { it.fluidIterations }) { p, v -> p.copy(fluidIterations = v) },
            scalar("fluidCurl", ParamKeys.FLUID_CURL, { it.fluidCurl }) { p, v -> p.copy(fluidCurl = v) },
            scalar("fluidVelocityDissipation", ParamKeys.MOTION_FADE, {
                it.fluidVelocityDissipation
            }) { p, v -> p.copy(fluidVelocityDissipation = v) },
            scalar("fluidDensityDissipation", ParamKeys.FLUID_FADE, {
                it.fluidDensityDissipation
            }) { p, v -> p.copy(fluidDensityDissipation = v) },
            scalar(
                "fluidChromaticAging",
                ParamKeys.CHROMATIC_AGING,
                { it.fluidChromaticAging },
            ) { p, v -> p.copy(fluidChromaticAging = v) },
            scalar("fluidPressure", ParamKeys.PRESSURE, { it.fluidPressure }) { p, v -> p.copy(fluidPressure = v) },
            choice("fluidBeatPattern", ParamKeys.BEAT_PATTERN, SceneParams.FLUID_PATTERNS, {
                it.fluidBeatPattern
            }) { p, v -> p.copy(fluidBeatPattern = v) },
            count("fluidBeatSplats", ParamKeys.BEAT_SPLATS, { it.fluidBeatSplats }) { p, v -> p.copy(fluidBeatSplats = v) },
            count("fluidStirrers", ParamKeys.STIRRERS, { it.fluidStirrers }) { p, v -> p.copy(fluidStirrers = v) },
            scalar(
                "fluidStirrerSpeed",
                ParamKeys.STIRRER_SPEED,
                { it.fluidStirrerSpeed },
            ) { p, v -> p.copy(fluidStirrerSpeed = v) },
            scalar(
                "fluidSplatRadius",
                ParamKeys.FLUID_SPLAT_RADIUS,
                { it.fluidSplatRadius },
            ) { p, v -> p.copy(fluidSplatRadius = v) },
            scalar("fluidRadiusPulse", ParamKeys.RADIUS_ON_BEAT, { it.fluidRadiusPulse }) { p, v -> p.copy(fluidRadiusPulse = v) },
            scalar("fluidSplatForce", ParamKeys.FLUID_SPLAT_FORCE, { it.fluidSplatForce }) { p, v -> p.copy(fluidSplatForce = v) },
            toggle("fluidBassPump", ParamKeys.BASS_PUMP, { it.fluidBassPump }) { p, v -> p.copy(fluidBassPump = v) },
            toggle("fluidSparkle", ParamKeys.TREBLE_SPARKLE, { it.fluidSparkle }) { p, v -> p.copy(fluidSparkle = v) },
            scalar("fluidPaletteCycleSpeed", ParamKeys.PALETTE_CYCLE, {
                it.fluidPaletteCycleSpeed
            }) { p, v -> p.copy(fluidPaletteCycleSpeed = v) },
            scalar(
                "fluidParticleDrag",
                ParamKeys.PARTICLE_DRAG,
                { it.fluidParticleDrag },
            ) { p, v -> p.copy(fluidParticleDrag = v) },
            scalar(
                "fluidParticleLife",
                ParamKeys.PARTICLE_LIFE_S,
                { it.fluidParticleLife },
            ) { p, v -> p.copy(fluidParticleLife = v) },
            scalar("fluidParticleBrightness", ParamKeys.PARTICLE_BRIGHTNESS, {
                it.fluidParticleBrightness
            }) { p, v -> p.copy(fluidParticleBrightness = v) },
            toggle("fluidShading", ParamKeys.SHADING_EMBOSSED_INK, { it.fluidShading }) { p, v -> p.copy(fluidShading = v) },
            toggle("fluidBloom", ParamKeys.GLOW_FLUID, { it.fluidBloom }) { p, v -> p.copy(fluidBloom = v) },
            scalar(
                "fluidBloomIntensity",
                ParamKeys.FLUID_GLOW,
                { it.fluidBloomIntensity },
            ) { p, v -> p.copy(fluidBloomIntensity = v) },
            scalar(
                "fluidBloomThreshold",
                ParamKeys.GLOW_THRESHOLD,
                { it.fluidBloomThreshold },
            ) { p, v -> p.copy(fluidBloomThreshold = v) },
            toggle("fluidSunrays", ParamKeys.SUNRAYS, { it.fluidSunrays }) { p, v -> p.copy(fluidSunrays = v) },
            scalar(
                "fluidSunraysWeight",
                ParamKeys.SUNRAYS_WEIGHT,
                { it.fluidSunraysWeight },
            ) { p, v -> p.copy(fluidSunraysWeight = v) },
            scalar("fluidCurlAudio", ParamKeys.CURL_FROM_MIDS, { it.fluidCurlAudio }) { p, v -> p.copy(fluidCurlAudio = v) },
            scalar("fluidBloomAudio", ParamKeys.GLOW_FROM_LOUDNESS, { it.fluidBloomAudio }) { p, v -> p.copy(fluidBloomAudio = v) },
            scalar("fluidFadeAudio", ParamKeys.FADE_WHEN_QUIET, { it.fluidFadeAudio }) { p, v -> p.copy(fluidFadeAudio = v) },
            scalar("flowStrength", ParamKeys.FLOW_STRENGTH, { it.flowStrength }) { p, v -> p.copy(flowStrength = v) },
            scalar("flowForce", ParamKeys.FLOW_FORCE, { it.flowForce }) { p, v -> p.copy(flowForce = v) },
            scalar("flowCurl", ParamKeys.FLOW_CURL, { it.flowCurl }) { p, v -> p.copy(flowCurl = v) },
            scalar("rippleOverlayStrength", ParamKeys.RIPPLE_OVERLAY_STRENGTH, {
                it.rippleOverlayStrength
            }) { p, v -> p.copy(rippleOverlayStrength = v) },
            scalar(
                "rippleOverlaySpecular",
                ParamKeys.RIPPLE_GLINT,
                { it.rippleOverlaySpecular },
            ) { p, v -> p.copy(rippleOverlaySpecular = v) },
            choice("cymaticsGeometry", ParamKeys.GEOMETRY, SceneParams.CYMATICS_GEOMETRIES, {
                it.cymaticsGeometry
            }) { p, v -> p.copy(cymaticsGeometry = v) },
            scalar("cymaticsFundamental", ParamKeys.FUNDAMENTAL_HZ, {
                it.cymaticsFundamental
            }) { p, v -> p.copy(cymaticsFundamental = v) },
            count("cymaticsModes", ParamKeys.STANDING_WAVES, {
                it.cymaticsModes
            }) { p, v -> p.copy(cymaticsModes = v) },
            scalar("cymaticsFocus", ParamKeys.TONAL_FOCUS, { it.cymaticsFocus }) { p, v -> p.copy(cymaticsFocus = v) },
            scalar("cymaticsRing", ParamKeys.PLATE_RING, { it.cymaticsRing }) { p, v -> p.copy(cymaticsRing = v) },
            scalar("cymaticsScale", ParamKeys.FIELD_SCALE, { it.cymaticsScale }) { p, v -> p.copy(cymaticsScale = v) },
            scalar("cymaticsFlow", ParamKeys.WAVE_FLOW, { it.cymaticsFlow }) { p, v -> p.copy(cymaticsFlow = v) },
            scalar("cymaticsSwirl", ParamKeys.FIELD_SWIRL, { it.cymaticsSwirl }) { p, v -> p.copy(cymaticsSwirl = v) },
            scalar("cymaticsFill", ParamKeys.FILL, { it.cymaticsFill }) { p, v -> p.copy(cymaticsFill = v) },
            scalar("cymaticsLine", ParamKeys.NODAL_LINES, { it.cymaticsLine }) { p, v -> p.copy(cymaticsLine = v) },
            scalar("cymaticsGlow", ParamKeys.NODAL_GLOW, { it.cymaticsGlow }) { p, v -> p.copy(cymaticsGlow = v) },
            scalar(
                "cymaticsIridescence",
                ParamKeys.IRIDESCENCE,
                { it.cymaticsIridescence },
            ) { p, v -> p.copy(cymaticsIridescence = v) },
            scalar("cymaticsCaustic", ParamKeys.CAUSTIC_SHEEN, { it.cymaticsCaustic }) { p, v -> p.copy(cymaticsCaustic = v) },
        )
}
