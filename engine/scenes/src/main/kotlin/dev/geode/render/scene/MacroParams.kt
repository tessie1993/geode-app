package dev.geode.render.scene

import dev.geode.render.LfoTarget

/**
 * A Customize control a macro can drive: its [ParamKeys] name, the range its slider covers, and
 * how to read and write it.
 *
 * [span] is the unit a macro destination's amount is measured in (an amount of 0.5 moves the
 * parameter half its slider per full turn), so one amount means the same "how far" on a speed
 * slider and on a bloom slider. [reactTarget] is the reaction-matrix target that moves the same
 * field, if there is one, and [reactScale] is the factor the renderer multiplies a reaction by on
 * that target (core/viz/Lfo.cpp), so a macro's sound can be turned into routes in parameter units.
 */
class MacroParam(
    val key: String,
    val range: ClosedFloatingPointRange<Float>,
    val reactTarget: LfoTarget?,
    val reactScale: Float,
    val get: (SceneParams) -> Float,
    val put: (SceneParams, Float) -> SceneParams,
) {
    val span: Float get() = range.endInclusive - range.start

    val scope: ParamScope get() = ParamScope.of(key)
}

/**
 * Every control a macro can drive, in the ranges the Customize sliders use.
 *
 * The floats only: a palette, a particle shape or a toggle has no "a bit more" for a knob to
 * turn. Performance budgets and input preferences (Detail, camera smoothing, band gains) are left
 * out for the same reason the randomizer leaves them alone.
 */
object MacroParams {
    private fun p(
        key: String,
        range: ClosedFloatingPointRange<Float>,
        target: LfoTarget?,
        scale: Float,
        get: (SceneParams) -> Float,
        put: (SceneParams, Float) -> SceneParams,
    ) = MacroParam(key, range, target, scale, get, put)

    private fun p(
        key: String,
        range: ClosedFloatingPointRange<Float>,
        target: LfoTarget?,
        get: (SceneParams) -> Float,
        put: (SceneParams, Float) -> SceneParams,
    ) = MacroParam(key, range, target, 1f, get, put)

    val ALL: List<MacroParam> =
        listOf(
            p(ParamKeys.AUDIO_DRIVE, 0.2f..2.5f, null, { it.audioDrive }) { s, v -> s.copy(audioDrive = v) },
            p(ParamKeys.BLOOM, 0f..0.25f, LfoTarget.BLOOM, { it.bloom }) { s, v -> s.copy(bloom = v) },
            p(ParamKeys.BRIGHTNESS, 0.2f..1.25f, LfoTarget.BRIGHTNESS, { it.brightness }) { s, v -> s.copy(brightness = v) },
            p(ParamKeys.CAM_FLY, 0f..2f, LfoTarget.CAM_FLY, { it.camFly }) { s, v -> s.copy(camFly = v) },
            p(ParamKeys.CAM_FOV, 0.5f..2f, LfoTarget.CAM_FOV, { it.camFov }) { s, v -> s.copy(camFov = v) },
            p(ParamKeys.CAM_ORBIT, 0f..1f, LfoTarget.CAM_ORBIT, { it.camOrbit }) { s, v -> s.copy(camOrbit = v) },
            p(ParamKeys.CAM_PITCH, -1f..1f, LfoTarget.CAM_PITCH, { it.camPitch }) { s, v -> s.copy(camPitch = v) },
            p(ParamKeys.CAM_PUSH, -1f..1f, LfoTarget.CAM_PUSH, { it.camPush }) { s, v -> s.copy(camPush = v) },
            p(ParamKeys.CAM_ROLL, -1f..1f, LfoTarget.CAM_ROLL, { it.camRoll }) { s, v -> s.copy(camRoll = v) },
            p(ParamKeys.CAM_SHAKE, 0f..1f, LfoTarget.CAM_SHAKE, { it.camShake }) { s, v -> s.copy(camShake = v) },
            p(ParamKeys.CAM_YAW, -1f..1f, LfoTarget.CAM_YAW, { it.camYaw }) { s, v -> s.copy(camYaw = v) },
            p(ParamKeys.CATCH_PULL, 0f..3f, LfoTarget.FLUID_CATCH_PULL, 1.5f, { it.fluidCatchPull }) { s, v -> s.copy(fluidCatchPull = v) },
            p(ParamKeys.CATCH_RADIUS, 0.03f..0.3f, LfoTarget.FLUID_CATCH_RADIUS, 0.12f, { it.fluidCatchRadius }) { s, v ->
                s.copy(fluidCatchRadius = v)
            },
            p(ParamKeys.CAUSTIC_SHEEN, 0f..1.5f, null, { it.cymaticsCaustic }) { s, v -> s.copy(cymaticsCaustic = v) },
            p(ParamKeys.CHROMATIC_ABERRATION, 0f..1f, LfoTarget.CHROMA_AB, { it.chromaAb }) { s, v -> s.copy(chromaAb = v) },
            p(ParamKeys.CHROMATIC_AGING, 0f..1f, null, { it.fluidChromaticAging }) { s, v -> s.copy(fluidChromaticAging = v) },
            p(ParamKeys.CONTRAST, 0.3f..1.25f, null, { it.contrast }) { s, v -> s.copy(contrast = v) },
            p(ParamKeys.CURL_FROM_MIDS, 0f..1f, null, { it.fluidCurlAudio }) { s, v -> s.copy(fluidCurlAudio = v) },
            p(ParamKeys.CYCLE_SPEED, 0.02f..0.6f, null, { it.cycleSpeed }) { s, v -> s.copy(cycleSpeed = v) },
            p(ParamKeys.DENSITY, 0.1f..1f, null, { it.density }) { s, v -> s.copy(density = v) },
            p(ParamKeys.DEPTH, 0f..1f, null, { it.waterDepth }) { s, v -> s.copy(waterDepth = v) },
            p(ParamKeys.DIVE_SPEED, 0.05f..1.2f, null, { it.endlessZoomSpeed }) { s, v -> s.copy(endlessZoomSpeed = v) },
            p(ParamKeys.DOMAIN_WARP, 0f..1f, LfoTarget.WARP, { it.warp }) { s, v -> s.copy(warp = v) },
            p(ParamKeys.DRIFT_X, -1f..1f, LfoTarget.DRIFT_X, { it.driftX }) { s, v -> s.copy(driftX = v) },
            p(ParamKeys.DRIFT_Y, -1f..1f, LfoTarget.DRIFT_Y, { it.driftY }) { s, v -> s.copy(driftY = v) },
            p(ParamKeys.FADE_WHEN_QUIET, 0f..1f, null, { it.fluidFadeAudio }) { s, v -> s.copy(fluidFadeAudio = v) },
            p(ParamKeys.FIELD_SCALE, 0.5f..8f, null, { it.cymaticsScale }) { s, v -> s.copy(cymaticsScale = v) },
            p(ParamKeys.FIELD_SWIRL, -1f..1f, null, { it.cymaticsSwirl }) { s, v -> s.copy(cymaticsSwirl = v) },
            p(ParamKeys.FILL, 0f..1f, null, { it.cymaticsFill }) { s, v -> s.copy(cymaticsFill = v) },
            p(ParamKeys.FILM_GRAIN, 0f..1f, null, { it.grain }) { s, v -> s.copy(grain = v) },
            p(ParamKeys.FISHEYE, -1f..1f, LfoTarget.FISHEYE, { it.fisheye }) { s, v -> s.copy(fisheye = v) },
            p(ParamKeys.FLOW_CURL, 0f..50f, null, { it.flowCurl }) { s, v -> s.copy(flowCurl = v) },
            p(ParamKeys.FLOW_DRIFT, 0f..1f, null, { it.waterFlow }) { s, v -> s.copy(waterFlow = v) },
            p(ParamKeys.FLOW_FORCE, 0f..3f, null, { it.flowForce }) { s, v -> s.copy(flowForce = v) },
            p(ParamKeys.FLOW_STRENGTH, 0f..1f, LfoTarget.FLOW_STRENGTH, { it.flowStrength }) { s, v -> s.copy(flowStrength = v) },
            p(ParamKeys.FLUID_CURL, 0f..50f, LfoTarget.FLUID_CURL, 25.0f, { it.fluidCurl }) { s, v -> s.copy(fluidCurl = v) },
            p(ParamKeys.FLUID_FADE, 0f..4f, LfoTarget.FLUID_FADE, 1.5f, { it.fluidDensityDissipation }) { s, v ->
                s.copy(fluidDensityDissipation = v)
            },
            p(ParamKeys.FLUID_GLOW, 0.1f..2f, LfoTarget.FLUID_GLOW, { it.fluidBloomIntensity }) { s, v -> s.copy(fluidBloomIntensity = v) },
            p(ParamKeys.FLUID_SPLAT_FORCE, 0f..3f, LfoTarget.FLUID_FORCE, 1.5f, { it.fluidSplatForce }) { s, v ->
                s.copy(fluidSplatForce = v)
            },
            p(ParamKeys.FLUID_SPLAT_RADIUS, 0.02f..0.4f, LfoTarget.FLUID_RADIUS, 0.15f, { it.fluidSplatRadius }) { s, v ->
                s.copy(fluidSplatRadius = v)
            },
            p(ParamKeys.GAMMA, 0.3f..2.5f, null, { it.gamma }) { s, v -> s.copy(gamma = v) },
            p(ParamKeys.GLITCH, 0f..0.25f, LfoTarget.GLITCH, { it.glitch }) { s, v -> s.copy(glitch = v) },
            p(ParamKeys.GLOW_FROM_LOUDNESS, 0f..1f, null, { it.fluidBloomAudio }) { s, v -> s.copy(fluidBloomAudio = v) },
            p(ParamKeys.HUE_RANGE, 0f..1.5f, null, { it.hueRange }) { s, v -> s.copy(hueRange = v) },
            p(ParamKeys.HUE_SHIFT, 0f..1f, LfoTarget.COLOR_SHIFT, 0.5f, { it.colorShift }) { s, v -> s.copy(colorShift = v) },
            p(ParamKeys.INTENSITY, 0.2f..1.25f, LfoTarget.INTENSITY, { it.intensity }) { s, v -> s.copy(intensity = v) },
            p(ParamKeys.IRIDESCENCE, 0f..1f, null, { it.cymaticsIridescence }) { s, v -> s.copy(cymaticsIridescence = v) },
            p(ParamKeys.LIQUID, 0f..1f, null, { it.waterLiquid }) { s, v -> s.copy(waterLiquid = v) },
            p(ParamKeys.LIQUID_FADE, 0f..2f, null, { it.waterLiquidFade }) { s, v -> s.copy(waterLiquidFade = v) },
            p(ParamKeys.LIQUID_FLOW, 0f..4f, null, { it.waterLiquidFlow }) { s, v -> s.copy(waterLiquidFlow = v) },
            p(ParamKeys.MORPH, 0f..1f, LfoTarget.MORPH, { it.morph }) { s, v -> s.copy(morph = v) },
            p(ParamKeys.MOTION_AMOUNT, 0f..1f, null, { it.motionAmount }) { s, v -> s.copy(motionAmount = v) },
            p(ParamKeys.MOTION_BREATH, 0f..1f, null, { it.motionBreath }) { s, v -> s.copy(motionBreath = v) },
            p(ParamKeys.MOTION_DRIFT, 0f..1f, null, { it.motionDrift }) { s, v -> s.copy(motionDrift = v) },
            p(ParamKeys.MOTION_FADE, 0f..4f, null, { it.fluidVelocityDissipation }) { s, v -> s.copy(fluidVelocityDissipation = v) },
            p(ParamKeys.MOTION_HUE, 0f..1f, null, { it.motionHue }) { s, v -> s.copy(motionHue = v) },
            p(ParamKeys.NODAL_GLOW, 0f..2f, null, { it.cymaticsGlow }) { s, v -> s.copy(cymaticsGlow = v) },
            p(ParamKeys.NODAL_LINES, 0f..2f, null, { it.cymaticsLine }) { s, v -> s.copy(cymaticsLine = v) },
            p(ParamKeys.PALETTE_BLEND, 0f..1f, LfoTarget.PALETTE_MIX, { it.paletteMix }) { s, v -> s.copy(paletteMix = v) },
            p(ParamKeys.PALETTE_CYCLE, 0f..2f, null, { it.fluidPaletteCycleSpeed }) { s, v -> s.copy(fluidPaletteCycleSpeed = v) },
            p(ParamKeys.PARTICLE_SIZE, 0.3f..2.5f, LfoTarget.PARTICLE_SIZE, { it.particleSize }) { s, v -> s.copy(particleSize = v) },
            p(ParamKeys.PIXELATE, 0f..1f, LfoTarget.PIXELATE, { it.pixelate }) { s, v -> s.copy(pixelate = v) },
            p(ParamKeys.PLATE_RING, 0f..1f, null, { it.cymaticsRing }) { s, v -> s.copy(cymaticsRing = v) },
            p(ParamKeys.POSTERIZE, 0f..1f, LfoTarget.POSTERIZE, { it.posterize }) { s, v -> s.copy(posterize = v) },
            p(ParamKeys.PRESSURE, 0f..1f, null, { it.fluidPressure }) { s, v -> s.copy(fluidPressure = v) },
            p(ParamKeys.RADIUS_ON_BEAT, 0f..1f, null, { it.fluidRadiusPulse }) { s, v -> s.copy(fluidRadiusPulse = v) },
            p(ParamKeys.REACT_AMOUNT, 0f..2f, null, { it.reactAmount }) { s, v -> s.copy(reactAmount = v) },
            p(ParamKeys.RIPPLE, 0f..1f, LfoTarget.RIPPLE, { it.ripple }) { s, v -> s.copy(ripple = v) },
            p(ParamKeys.RIPPLE_GLINT, 0f..1f, null, { it.rippleOverlaySpecular }) { s, v -> s.copy(rippleOverlaySpecular = v) },
            p(ParamKeys.RIPPLE_STRENGTH, 0f..2f, LfoTarget.WATER_RIPPLE, { it.waterRippleStrength }) { s, v ->
                s.copy(waterRippleStrength = v)
            },
            p(ParamKeys.ROTATION, -3f..3f, LfoTarget.ROTATION, 1.5f, { it.rotation }) { s, v -> s.copy(rotation = v) },
            p(ParamKeys.SATURATION, 0f..1.5f, LfoTarget.SATURATION, { it.saturation }) { s, v -> s.copy(saturation = v) },
            p(ParamKeys.SCANLINES, 0f..1f, null, { it.scanlines }) { s, v -> s.copy(scanlines = v) },
            p(ParamKeys.SPECULAR, 0f..1f, null, { it.waterSpecular }) { s, v -> s.copy(waterSpecular = v) },
            p(ParamKeys.SPEED, 0.05f..4f, LfoTarget.SPEED, { it.speed }) { s, v -> s.copy(speed = v) },
            p(ParamKeys.STIRRER_SPEED, 0f..2f, null, { it.fluidStirrerSpeed }) { s, v -> s.copy(fluidStirrerSpeed = v) },
            p(ParamKeys.SWAY, 0f..1f, LfoTarget.SWAY, { it.sway }) { s, v -> s.copy(sway = v) },
            p(ParamKeys.TEMPERATURE, -1f..1f, LfoTarget.TEMPERATURE, { it.temperature }) { s, v -> s.copy(temperature = v) },
            p(ParamKeys.TILE, 1f..6f, LfoTarget.TILE, 2.0f, { it.tile }) { s, v -> s.copy(tile = v) },
            p(ParamKeys.TONAL_FOCUS, 0f..1f, null, { it.cymaticsFocus }) { s, v -> s.copy(cymaticsFocus = v) },
            p(ParamKeys.TRAIL_LENGTH, 0.05f..0.98f, LfoTarget.TRAIL_LENGTH, { it.trailLength }) { s, v -> s.copy(trailLength = v) },
            p(ParamKeys.TRAIL_WARP_LIQUID_ECHO, 0f..1f, null, { it.trailWarp }) { s, v -> s.copy(trailWarp = v) },
            p(ParamKeys.TRAIL_ZOOM_ECHO_IN_OUT, -0.5f..0.5f, null, { it.trailZoom }) { s, v -> s.copy(trailZoom = v) },
            p(ParamKeys.TURBULENCE, 0f..1.5f, LfoTarget.TURBULENCE, { it.turbulence }) { s, v -> s.copy(turbulence = v) },
            p(ParamKeys.TWIST, -1f..1f, LfoTarget.TWIST, { it.twist }) { s, v -> s.copy(twist = v) },
            p(ParamKeys.VIGNETTE, 0f..1f, LfoTarget.VIGNETTE, { it.vignette }) { s, v -> s.copy(vignette = v) },
            p(ParamKeys.WAVE_FLOW, 0f..1f, null, { it.cymaticsFlow }) { s, v -> s.copy(cymaticsFlow = v) },
            p(ParamKeys.WAVE_SPEED, 0.2f..2f, null, { it.waterWaveSpeed }) { s, v -> s.copy(waterWaveSpeed = v) },
            p(ParamKeys.ZOOM, 0.3f..3f, LfoTarget.ZOOM, { it.zoom }) { s, v -> s.copy(zoom = v) },
        )

    private val BY_KEY: Map<String, MacroParam> = ALL.associateBy { it.key }

    fun of(key: String): MacroParam? = BY_KEY[key]

    /** The ones the style [sceneId] can show, for the editor's picker. */
    fun liveOn(sceneId: String): List<MacroParam> = ALL.filter { it.scope.appliesTo(sceneId) }
}
