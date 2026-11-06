package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.AutoDirectorStore
import dev.geode.data.MacroStore
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.HandReactions
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.fluid.FluidQuality
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.ParamUnit
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneParams
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.UiSound

/**
 * Draws one Customize control by the name core/viz/CustomizeMap files it under: a ParamKeys
 * string, or `@id` for a panel piece. Which section a control sits in, whether it is free, what
 * Randomize and Reset do to it: all native. This only picks the widget, its range and its words,
 * and hands the new value to the one write path ([VisualsViewModel.editSceneParams]).
 *
 * Every widget hides itself on a style it cannot move ([visible] through the catalogue's scope) and
 * asks [LocalProLock] whether this user may keep a change, so a Pro control can still be tried.
 */
@Composable
internal fun CustomizeControl(
    key: String,
    host: AdvancedHost,
) {
    val viz by host.viewModel.vizState.collectAsStateWithLifecycle()
    val p = viz.params
    val onChange: (SceneParams) -> Unit = host.visualsViewModel::editSceneParams
    if (key.startsWith("@")) {
        PanelPiece(key, host, p, onChange)
        return
    }
    when (CustomizeMapTable.categoryOf(key)) {
        CustomizeCategory.LOOK -> LookControl(key, host, p, onChange)
        CustomizeCategory.SHAPE -> ShapeControl(key, p, onChange)
        CustomizeCategory.MOTION, CustomizeCategory.CAMERA -> MotionControl(key, p, onChange)
        CustomizeCategory.MUSIC, CustomizeCategory.EFFECTS -> MusicControl(key, p, onChange)
        CustomizeCategory.LIQUID, CustomizeCategory.BEHAVIOUR -> LiquidControl(key, host, p, onChange)
        CustomizeCategory.EXPERT -> EngineControl(key, p, onChange)
        CustomizeCategory.QUICK_KNOBS, null -> Unit
    }
}

@Composable
private fun LookControl(
    key: String,
    host: AdvancedHost,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val d = SceneParams.DEFAULT
    when (key) {
        ParamKeys.PALETTE -> PaletteControl(host, p, onChange)
        ParamKeys.BRIGHTNESS ->
            // Upper ends are the engine's flash clamp (1 + `kMaxFlashDepth` in core/viz/VisualSafety.hpp).
            LabeledSlider(key, p.brightness, 0.2f..1.25f, default = d.brightness) { onChange(p.copy(brightness = it)) }
        ParamKeys.SATURATION -> LabeledSlider(key, p.saturation, 0f..1.5f, default = d.saturation) { onChange(p.copy(saturation = it)) }
        ParamKeys.BLOOM -> LabeledSlider(key, p.bloom, 0f..0.25f, display = "Glow", default = d.bloom) { onChange(p.copy(bloom = it)) }
        ParamKeys.PALETTE_2 ->
            if (p.paletteMix > PALETTE_MIX_SHOWN && visible(key, ParamScope.SHADER_LOOK)) {
                ProGated(key) { PaletteDropdown(key, "Second palette", p, onChange, rememberSavedPalettes(), second = true) }
            }
        ParamKeys.PALETTE_BLEND -> LabeledSlider(key, p.paletteMix, 0f..1f, default = d.paletteMix) { onChange(p.copy(paletteMix = it)) }
        ParamKeys.COLOUR_MAP ->
            if (visible(key, ParamScope.SHADER_LOOK)) {
                val selected = SceneParams.CYCLIC_PALETTES.getOrNull(p.paletteLut) ?: "Off"
                ProGated(key, value = selected) {
                    DropdownRow(
                        name = key,
                        options = listOf("Off") + SceneParams.CYCLIC_PALETTES,
                        selectedIndex = if (p.paletteLut < 0) 0 else p.paletteLut + 1,
                        lockKey = key,
                        about = "Even colour steps that loop, so a wrap has no seam.",
                    ) { i -> onChange(p.copy(paletteLut = if (i == 0) SceneParams.NO_PALETTE_LUT else i - 1)) }
                }
            }
        ParamKeys.HUE_SHIFT -> LabeledSlider(key, p.colorShift, 0f..1f, default = d.colorShift) { onChange(p.copy(colorShift = it)) }
        ParamKeys.HUE_RANGE -> LabeledSlider(key, p.hueRange, 0f..1.5f, default = d.hueRange) { onChange(p.copy(hueRange = it)) }
        ParamKeys.TEMPERATURE ->
            LabeledSlider(key, p.temperature, -1f..1f, display = "Warmth", default = d.temperature) { onChange(p.copy(temperature = it)) }
        ParamKeys.COLOR_CYCLE -> CheckRow(key, p.colorCycle, display = "Cycle the colours") { onChange(p.copy(colorCycle = it)) }
        ParamKeys.CYCLE_SPEED ->
            if (p.colorCycle) {
                LabeledSlider(key, p.cycleSpeed, 0.02f..0.6f, default = d.cycleSpeed) { onChange(p.copy(cycleSpeed = it)) }
            }
        ParamKeys.CONTRAST -> LabeledSlider(key, p.contrast, 0.3f..1.25f, default = d.contrast) { onChange(p.copy(contrast = it)) }
        ParamKeys.GAMMA -> LabeledSlider(key, p.gamma, 0.3f..2.5f, display = "Midtones", default = d.gamma) { onChange(p.copy(gamma = it)) }
        ParamKeys.INTENSITY -> LabeledSlider(key, p.intensity, 0.2f..1.25f, default = d.intensity) { onChange(p.copy(intensity = it)) }
        else -> Unit
    }
}

@Composable
private fun ShapeControl(
    key: String,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val d = SceneParams.DEFAULT
    when (key) {
        ParamKeys.MORPH -> LabeledSlider(key, p.morph, 0f..1f, default = d.morph) { onChange(p.copy(morph = it)) }
        ParamKeys.KALEIDOSCOPE -> KaleidoscopeControl(p, onChange)
        ParamKeys.MARCH_DETAIL ->
            LabeledSlider(key, p.marchDetail, Catalogue.range(key), default = d.marchDetail) { onChange(p.copy(marchDetail = it)) }
        ParamKeys.DOMAIN_WARP -> LabeledSlider(key, p.warp, 0f..1f, display = "Warp", default = d.warp) { onChange(p.copy(warp = it)) }
        ParamKeys.TWIST -> LabeledSlider(key, p.twist, -1f..1f, default = d.twist) { onChange(p.copy(twist = it)) }
        ParamKeys.RIPPLE -> LabeledSlider(key, p.ripple, 0f..1f, default = d.ripple) { onChange(p.copy(ripple = it)) }
        ParamKeys.TURBULENCE -> LabeledSlider(key, p.turbulence, 0f..1.5f, default = d.turbulence) { onChange(p.copy(turbulence = it)) }
        ParamKeys.MIRROR -> CheckRow(key, p.mirror) { onChange(p.copy(mirror = it)) }
        ParamKeys.TILE -> LabeledSlider(key, p.tile, 1f..6f, default = d.tile) { onChange(p.copy(tile = it)) }
        ParamKeys.PIXELATE -> LabeledSlider(key, p.pixelate, 0f..1f, default = d.pixelate) { onChange(p.copy(pixelate = it)) }
        ParamKeys.PARTICLE_SHAPE -> ParamChips(key, SceneParams.PARTICLE_SHAPES, p.particleShape) { onChange(p.copy(particleShape = it)) }
        ParamKeys.PARTICLE_SIZE ->
            LabeledSlider(key, p.particleSize, 0.3f..2.5f, default = d.particleSize) { onChange(p.copy(particleSize = it)) }
        ParamKeys.DENSITY -> LabeledSlider(key, p.density, 0.1f..1f, default = d.density) { onChange(p.copy(density = it)) }
        ParamKeys.PARTICLE_LIFE_S ->
            LabeledSlider(key, p.fluidParticleLife, 1f..20f, default = d.fluidParticleLife) { onChange(p.copy(fluidParticleLife = it)) }
        ParamKeys.PARTICLE_DRAG ->
            LabeledSlider(key, p.fluidParticleDrag, 0.02f..1f, default = d.fluidParticleDrag) { onChange(p.copy(fluidParticleDrag = it)) }
        ParamKeys.PATH ->
            ParamChips(key, SceneParams.FLUID_PATHS, p.fluidSpawnPath.coerceIn(0, SceneParams.FLUID_PATHS.size - 1)) {
                onChange(p.copy(fluidSpawnPath = it))
            }
        ParamKeys.SPAWN_POINTS -> LabeledIntSlider(key, p.fluidSpawnPoints, 1..8) { onChange(p.copy(fluidSpawnPoints = it)) }
        PROGRESSION ->
            LabeledSlider(key, p.fluidSpawnProgress, 0f..1f, scope = ParamScope.JOURNEY, default = d.fluidSpawnProgress) {
                onChange(p.copy(fluidSpawnProgress = it))
            }
        ParamKeys.CATCH_POINTS -> LabeledIntSlider(key, p.fluidCatchPoints, 0..4) { onChange(p.copy(fluidCatchPoints = it)) }
        ParamKeys.CATCH_PULL ->
            if (p.fluidCatchPoints > 0) {
                LabeledSlider(key, p.fluidCatchPull, 0f..3f, default = d.fluidCatchPull) { onChange(p.copy(fluidCatchPull = it)) }
            }
        ParamKeys.CATCH_RADIUS ->
            if (p.fluidCatchPoints > 0) {
                LabeledSlider(key, p.fluidCatchRadius, 0.03f..0.3f, default = d.fluidCatchRadius) {
                    onChange(p.copy(fluidCatchRadius = it))
                }
            }
        ParamKeys.BEAT_PATTERN ->
            ParamChips(key, SceneParams.FLUID_PATTERNS, p.fluidBeatPattern.coerceIn(0, 3)) { onChange(p.copy(fluidBeatPattern = it)) }
        ParamKeys.BEAT_SPLATS -> LabeledIntSlider(key, p.fluidBeatSplats, 0..8) { onChange(p.copy(fluidBeatSplats = it)) }
        ParamKeys.STIRRERS -> LabeledIntSlider(key, p.fluidStirrers, 0..4) { onChange(p.copy(fluidStirrers = it)) }
        ParamKeys.STIRRER_SPEED ->
            LabeledSlider(key, p.fluidStirrerSpeed, 0f..2f, default = d.fluidStirrerSpeed) { onChange(p.copy(fluidStirrerSpeed = it)) }
        ParamKeys.FLUID_SPLAT_RADIUS ->
            LabeledSlider(key, p.fluidSplatRadius, 0.02f..0.4f, default = d.fluidSplatRadius) { onChange(p.copy(fluidSplatRadius = it)) }
        ParamKeys.RADIUS_ON_BEAT ->
            LabeledSlider(key, p.fluidRadiusPulse, 0f..1f, default = d.fluidRadiusPulse) { onChange(p.copy(fluidRadiusPulse = it)) }
        ParamKeys.FLUID_SPLAT_FORCE ->
            LabeledSlider(key, p.fluidSplatForce, 0f..3f, default = d.fluidSplatForce) { onChange(p.copy(fluidSplatForce = it)) }
        ParamKeys.BASS_PUMP -> CheckRow(key, p.fluidBassPump) { onChange(p.copy(fluidBassPump = it)) }
        ParamKeys.TREBLE_SPARKLE -> CheckRow(key, p.fluidSparkle) { onChange(p.copy(fluidSparkle = it)) }
        FLUID_QUALITY ->
            if (visible(key, ParamScope.EMITTERS)) {
                val labels = FluidQuality.LABELS
                val quality = p.fluidQuality.coerceIn(0, labels.size - 1)
                ProGated(key, value = labels[quality]) { DropdownRow(key, labels, quality) { onChange(p.copy(fluidQuality = it)) } }
            }
        AUTO_QUALITY -> CheckRow(key, p.fluidAutoQuality, scope = ParamScope.EMITTERS) { onChange(p.copy(fluidAutoQuality = it)) }
        else -> Unit
    }
}

@Composable
private fun MotionControl(
    key: String,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val d = SceneParams.DEFAULT
    when (key) {
        ParamKeys.SPEED -> LabeledSlider(key, p.speed, 0.05f..4f, default = d.speed) { onChange(p.copy(speed = it)) }
        ParamKeys.ZOOM -> LabeledSlider(key, p.zoom, 0.3f..3f, default = d.zoom) { onChange(p.copy(zoom = it)) }
        ParamKeys.ROTATION ->
            LabeledSlider(key, p.rotation, -3f..3f, display = "Spin", default = d.rotation) { onChange(p.copy(rotation = it)) }
        ParamKeys.SWAY -> LabeledSlider(key, p.sway, 0f..1f, default = d.sway) { onChange(p.copy(sway = it)) }
        ParamKeys.DRIFT_X ->
            LabeledSlider(key, p.driftX, -1f..1f, display = "Drift sideways", default = d.driftX) { onChange(p.copy(driftX = it)) }
        ParamKeys.DRIFT_Y ->
            LabeledSlider(key, p.driftY, -1f..1f, display = "Drift up and down", default = d.driftY) { onChange(p.copy(driftY = it)) }
        ParamKeys.ENDLESS_ZOOM -> CheckRow(key, p.endlessZoom) { onChange(p.copy(endlessZoom = it)) }
        ParamKeys.DIVE_SPEED ->
            // Liquid mode dives on any style, so Dive speed shows with it as well as with the endless zoom.
            if (p.endlessZoom || p.flowEnabled) {
                val scope = if (p.flowEnabled) ParamScope.UNIVERSAL else ParamScope.ENDLESS_ZOOM
                LabeledSlider(key, p.endlessZoomSpeed, 0.05f..1.2f, scope = scope, default = d.endlessZoomSpeed) {
                    onChange(p.copy(endlessZoomSpeed = it))
                }
            }
        ParamKeys.MOTION_AMOUNT ->
            LabeledSlider(key, p.motionAmount, 0f..1f, default = d.motionAmount) { onChange(p.copy(motionAmount = it)) }
        ParamKeys.MOTION_BREATH ->
            LabeledSlider(key, p.motionBreath, 0f..1f, default = d.motionBreath) { onChange(p.copy(motionBreath = it)) }
        ParamKeys.MOTION_DRIFT -> LabeledSlider(key, p.motionDrift, 0f..1f, default = d.motionDrift) { onChange(p.copy(motionDrift = it)) }
        ParamKeys.CAM_PATH ->
            ParamChips(key, SceneParams.CAMERA_PATHS, p.camPath.coerceIn(0, SceneParams.CAMERA_PATHS.lastIndex), ParamScope.UNIVERSAL) {
                onChange(p.copy(camPath = it))
            }
        ParamKeys.CAM_FLY -> LabeledSlider(key, p.camFly, 0f..2f, default = d.camFly) { onChange(p.copy(camFly = it)) }
        ParamKeys.CAM_FOV -> LabeledSlider(key, p.camFov, 0.5f..2f, default = d.camFov) { onChange(p.copy(camFov = it)) }
        ParamKeys.CAM_ORBIT -> LabeledSlider(key, p.camOrbit, 0f..1f, default = d.camOrbit) { onChange(p.copy(camOrbit = it)) }
        ParamKeys.CAM_PUSH -> LabeledSlider(key, p.camPush, -1f..1f, default = d.camPush) { onChange(p.copy(camPush = it)) }
        ParamKeys.CAM_YAW -> LabeledSlider(key, p.camYaw, -1f..1f, default = d.camYaw) { onChange(p.copy(camYaw = it)) }
        ParamKeys.CAM_PITCH -> LabeledSlider(key, p.camPitch, -1f..1f, default = d.camPitch) { onChange(p.copy(camPitch = it)) }
        ParamKeys.CAM_ROLL -> LabeledSlider(key, p.camRoll, -1f..1f, default = d.camRoll) { onChange(p.copy(camRoll = it)) }
        ParamKeys.CAM_SHAKE -> LabeledSlider(key, p.camShake, 0f..1f, default = d.camShake) { onChange(p.copy(camShake = it)) }
        ParamKeys.CAM_GYRO -> CheckRow(key, p.camGyro) { onChange(p.copy(camGyro = it)) }
        ParamKeys.CAM_TOUCH -> CheckRow(key, p.camTouch) { onChange(p.copy(camTouch = it)) }
        ParamKeys.CAM_SMOOTH -> LabeledSlider(key, p.camSmooth, 0f..1f, default = d.camSmooth) { onChange(p.copy(camSmooth = it)) }
        else -> Unit
    }
}

@Composable
private fun MusicControl(
    key: String,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val d = SceneParams.DEFAULT
    when (key) {
        ParamKeys.AUDIO_DRIVE ->
            LabeledSlider(key, p.audioDrive, 0.2f..2.5f, display = "Sound strength", default = d.audioDrive) {
                onChange(p.copy(audioDrive = it))
            }
        ParamKeys.REACT_AMOUNT -> LabeledSlider(key, p.reactAmount, 0f..2f, default = d.reactAmount) { onChange(p.copy(reactAmount = it)) }
        ParamKeys.BASS_GAIN ->
            LabeledSlider(key, p.bassGain, 0f..2f, display = "Bass", default = d.bassGain) { onChange(p.copy(bassGain = it)) }
        ParamKeys.MID_GAIN ->
            LabeledSlider(key, p.midGain, 0f..2f, display = "Mids", default = d.midGain) { onChange(p.copy(midGain = it)) }
        ParamKeys.TREBLE_GAIN ->
            LabeledSlider(key, p.trebGain, 0f..2f, display = "Highs", default = d.trebGain) { onChange(p.copy(trebGain = it)) }
        ParamKeys.MOTION_HUE -> LabeledSlider(key, p.motionHue, 0f..1f, default = d.motionHue) { onChange(p.copy(motionHue = it)) }
        ParamKeys.TRAILS -> CheckRow(key, p.trails) { onChange(p.copy(trails = it)) }
        ParamKeys.VIGNETTE ->
            LabeledSlider(key, p.vignette, 0f..1f, display = "Dark edges", default = d.vignette) { onChange(p.copy(vignette = it)) }
        ParamKeys.TRAIL_LENGTH ->
            LabeledSlider(key, p.trailLength, 0.05f..0.98f, default = d.trailLength) { onChange(p.copy(trailLength = it)) }
        ParamKeys.TRAIL_ZOOM_ECHO_IN_OUT ->
            LabeledSlider(key, p.trailZoom, -0.5f..0.5f, display = "Trail zoom", default = d.trailZoom) { onChange(p.copy(trailZoom = it)) }
        ParamKeys.TRAIL_WARP_LIQUID_ECHO ->
            LabeledSlider(key, p.trailWarp, 0f..1f, display = "Trail ripple", default = d.trailWarp) { onChange(p.copy(trailWarp = it)) }
        ParamKeys.CHROMATIC_ABERRATION ->
            LabeledSlider(key, p.chromaAb, 0f..1f, display = "Colour fringe", default = d.chromaAb) { onChange(p.copy(chromaAb = it)) }
        ParamKeys.FISHEYE ->
            LabeledSlider(key, p.fisheye, -1f..1f, display = "Bulge", default = d.fisheye) { onChange(p.copy(fisheye = it)) }
        ParamKeys.FILM_GRAIN -> LabeledSlider(key, p.grain, 0f..1f, default = d.grain) { onChange(p.copy(grain = it)) }
        ParamKeys.SCANLINES -> LabeledSlider(key, p.scanlines, 0f..1f, default = d.scanlines) { onChange(p.copy(scanlines = it)) }
        ParamKeys.POSTERIZE ->
            LabeledSlider(key, p.posterize, 0f..1f, display = "Fewer colours", default = d.posterize) { onChange(p.copy(posterize = it)) }
        ParamKeys.DUOTONE -> CheckRow(key, p.duotone, display = "Two-colour") { onChange(p.copy(duotone = it)) }
        ParamKeys.SOLARIZE -> CheckRow(key, p.solarize) { onChange(p.copy(solarize = it)) }
        ParamKeys.INVERT -> CheckRow(key, p.invert, display = "Invert colours") { onChange(p.copy(invert = it)) }
        else -> Unit
    }
}

@Composable
private fun LiquidControl(
    key: String,
    host: AdvancedHost,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val d = SceneParams.DEFAULT
    when (key) {
        LIQUID_ON -> LiquidSwitch(host, p, onChange)
        ParamKeys.FLOW_STRENGTH ->
            if (p.flowEnabled) {
                LabeledSlider(key, p.flowStrength, 0f..1f, display = "Liquidity", scope = ParamScope.UNIVERSAL, default = d.flowStrength) {
                    onChange(p.copy(flowStrength = it))
                }
            }
        ParamKeys.FLOW_FORCE ->
            if (p.flowEnabled) {
                LabeledSlider(key, p.flowForce, 0f..3f, display = "Push", scope = ParamScope.UNIVERSAL, default = d.flowForce) {
                    onChange(p.copy(flowForce = it))
                }
            }
        ParamKeys.FLOW_CURL ->
            if (p.flowEnabled) {
                LabeledSlider(key, p.flowCurl, 0f..50f, display = "Swirl", scope = ParamScope.UNIVERSAL, default = d.flowCurl) {
                    onChange(p.copy(flowCurl = it))
                }
            }
        ParamKeys.LIQUID_BORN ->
            ParamChips(key, LIQUID_BORN_CHOICES, p.liquidBorn.coerceIn(0, 3), scope = ParamScope.UNIVERSAL) {
                onChange(p.copy(liquidBorn = it))
            }
        ParamKeys.LIQUID_AGE ->
            ParamChips(key, LIQUID_AGE_CHOICES, p.liquidAge.coerceIn(0, 2), scope = ParamScope.UNIVERSAL) {
                onChange(p.copy(liquidAge = it))
            }
        ParamKeys.LIQUID_SURFACE ->
            ParamChips(key, LIQUID_SURFACE_CHOICES, p.liquidSurface.coerceIn(0, 2), scope = ParamScope.UNIVERSAL) {
                onChange(p.copy(liquidSurface = it))
            }
        RIPPLES_ON ->
            CheckRow(key, p.rippleOverlayEnabled, scope = ParamScope.RIPPLE_OVERLAY) { onChange(p.copy(rippleOverlayEnabled = it)) }
        ParamKeys.RIPPLE_OVERLAY_STRENGTH ->
            if (p.rippleOverlayEnabled) {
                LabeledSlider(key, p.rippleOverlayStrength, 0f..1f, display = "Ripple strength", default = d.rippleOverlayStrength) {
                    onChange(p.copy(rippleOverlayStrength = it))
                }
            }
        ParamKeys.RIPPLE_GLINT ->
            if (p.rippleOverlayEnabled) {
                LabeledSlider(key, p.rippleOverlaySpecular, 0f..1f, default = d.rippleOverlaySpecular) {
                    onChange(p.copy(rippleOverlaySpecular = it))
                }
            }
        ParamKeys.WAVE_SPEED ->
            if (p.rippleOverlayEnabled || ParamScope.WATER.appliesTo(LocalSceneId.current)) {
                LabeledSlider(key, p.waterWaveSpeed, 0.2f..2f, default = d.waterWaveSpeed) { onChange(p.copy(waterWaveSpeed = it)) }
            }
        ParamKeys.DAMPING ->
            if (p.rippleOverlayEnabled || ParamScope.WATER.appliesTo(LocalSceneId.current)) {
                LabeledSlider(key, p.waterDamping, 0.9f..0.999f, display = "Calming", default = d.waterDamping) {
                    onChange(p.copy(waterDamping = it))
                }
            }
        ParamKeys.RIPPLE_STRENGTH ->
            LabeledSlider(key, p.waterRippleStrength, 0f..2f, default = d.waterRippleStrength) {
                onChange(p.copy(waterRippleStrength = it))
            }
        ParamKeys.DEPTH -> LabeledSlider(key, p.waterDepth, 0f..1f, default = d.waterDepth) { onChange(p.copy(waterDepth = it)) }
        ParamKeys.SPECULAR ->
            LabeledSlider(key, p.waterSpecular, 0f..1f, display = "Shine", default = d.waterSpecular) {
                onChange(p.copy(waterSpecular = it))
            }
        ParamKeys.FLOW_DRIFT -> LabeledSlider(key, p.waterFlow, 0f..1f, default = d.waterFlow) { onChange(p.copy(waterFlow = it)) }
        ParamKeys.LIQUID -> LabeledSlider(key, p.waterLiquid, 0f..1f, default = d.waterLiquid) { onChange(p.copy(waterLiquid = it)) }
        ParamKeys.LIQUID_FLOW ->
            if (p.waterLiquid > PALETTE_MIX_SHOWN) {
                LabeledSlider(key, p.waterLiquidFlow, 0f..4f, default = d.waterLiquidFlow) { onChange(p.copy(waterLiquidFlow = it)) }
            }
        ParamKeys.LIQUID_FADE ->
            if (p.waterLiquid > PALETTE_MIX_SHOWN) {
                LabeledSlider(key, p.waterLiquidFade, 0f..2f, default = d.waterLiquidFade) { onChange(p.copy(waterLiquidFade = it)) }
            }
        REACTION_DNA -> PersonalityControl(host)
        GLIDE_TIME ->
            LabeledSlider(key, p.paramFadeSec, 0f..5f, scope = ParamScope.UNIVERSAL, default = d.paramFadeSec, unit = ParamUnit.SECONDS) {
                onChange(p.copy(paramFadeSec = it))
            }
        else -> Unit
    }
}

/** The style's engine: Fluid's solver and look, Cymatics' plate, MilkDrop's hand-over. */
@Composable
private fun EngineControl(
    key: String,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val d = SceneParams.DEFAULT
    val sceneId = LocalSceneId.current
    when (key) {
        ParamKeys.SOLVER_ITERATIONS ->
            LabeledIntSlider(key, p.fluidIterations, 8..40, display = "Accuracy") { onChange(p.copy(fluidIterations = it)) }
        ParamKeys.FLUID_CURL ->
            if (SceneCapabilities.isFluid(sceneId)) {
                LabeledSlider(key, p.fluidCurl, 0f..50f, display = "Swirl", default = d.fluidCurl) { onChange(p.copy(fluidCurl = it)) }
            }
        ParamKeys.MOTION_FADE ->
            LabeledSlider(key, p.fluidVelocityDissipation, 0f..4f, default = d.fluidVelocityDissipation) {
                onChange(p.copy(fluidVelocityDissipation = it))
            }
        ParamKeys.FLUID_FADE ->
            LabeledSlider(key, p.fluidDensityDissipation, 0f..4f, default = d.fluidDensityDissipation) {
                onChange(p.copy(fluidDensityDissipation = it))
            }
        ParamKeys.CHROMATIC_AGING ->
            LabeledSlider(key, p.fluidChromaticAging, 0f..1f, display = "Colour ageing", default = d.fluidChromaticAging) {
                onChange(p.copy(fluidChromaticAging = it))
            }
        ParamKeys.PRESSURE ->
            LabeledSlider(key, p.fluidPressure, 0f..1f, default = d.fluidPressure) { onChange(p.copy(fluidPressure = it)) }
        ParamKeys.SHADING_EMBOSSED_INK -> CheckRow(key, p.fluidShading, display = "Embossed ink") { onChange(p.copy(fluidShading = it)) }
        ParamKeys.GLOW_FLUID -> CheckRow(key, p.fluidBloom, display = "Glow") { onChange(p.copy(fluidBloom = it)) }
        ParamKeys.FLUID_GLOW ->
            if (p.fluidBloom) {
                LabeledSlider(key, p.fluidBloomIntensity, 0.1f..2f, display = "Glow amount", default = d.fluidBloomIntensity) {
                    onChange(p.copy(fluidBloomIntensity = it))
                }
            }
        ParamKeys.GLOW_THRESHOLD ->
            if (p.fluidBloom) {
                LabeledSlider(key, p.fluidBloomThreshold, 0f..1f, default = d.fluidBloomThreshold) {
                    onChange(p.copy(fluidBloomThreshold = it))
                }
            }
        ParamKeys.SUNRAYS -> CheckRow(key, p.fluidSunrays) { onChange(p.copy(fluidSunrays = it)) }
        ParamKeys.SUNRAYS_WEIGHT ->
            if (p.fluidSunrays) {
                LabeledSlider(key, p.fluidSunraysWeight, 0.3f..1f, display = "Sunray strength", default = d.fluidSunraysWeight) {
                    onChange(p.copy(fluidSunraysWeight = it))
                }
            }
        ParamKeys.CURL_FROM_MIDS ->
            LabeledSlider(key, p.fluidCurlAudio, 0f..1f, display = "Swirl from the mids", default = d.fluidCurlAudio) {
                onChange(p.copy(fluidCurlAudio = it))
            }
        ParamKeys.GLOW_FROM_LOUDNESS ->
            LabeledSlider(key, p.fluidBloomAudio, 0f..1f, display = "Glow from volume", default = d.fluidBloomAudio) {
                onChange(p.copy(fluidBloomAudio = it))
            }
        ParamKeys.FADE_WHEN_QUIET ->
            LabeledSlider(key, p.fluidFadeAudio, 0f..1f, default = d.fluidFadeAudio) { onChange(p.copy(fluidFadeAudio = it)) }
        ParamKeys.PALETTE_CYCLE ->
            LabeledSlider(key, p.fluidPaletteCycleSpeed, 0f..2f, default = d.fluidPaletteCycleSpeed) {
                onChange(p.copy(fluidPaletteCycleSpeed = it))
            }
        ParamKeys.PARTICLE_BRIGHTNESS ->
            LabeledSlider(key, p.fluidParticleBrightness, 0f..2f, default = d.fluidParticleBrightness) {
                onChange(p.copy(fluidParticleBrightness = it))
            }
        PARTICLE_LAYER ->
            CheckRow(key, p.fluidParticlesEnabled, scope = ParamScope.FLUID_SIM) { onChange(p.copy(fluidParticlesEnabled = it)) }
        INK_LAYER -> CheckRow(key, p.fluidDyeEnabled, scope = ParamScope.FLUID_SIM) { onChange(p.copy(fluidDyeEnabled = it)) }
        ParamKeys.GEOMETRY -> CymaticsGeometry(p, onChange)
        ParamKeys.FUNDAMENTAL_HZ ->
            LabeledSlider(key, p.cymaticsFundamental, Catalogue.range(key), display = "Base note (Hz)") {
                onChange(p.copy(cymaticsFundamental = it))
            }
        ParamKeys.STANDING_WAVES ->
            LabeledIntSlider(key, p.cymaticsModes, 1..Catalogue.range(key).endInclusive.toInt()) { onChange(p.copy(cymaticsModes = it)) }
        ParamKeys.TONAL_FOCUS ->
            LabeledSlider(key, p.cymaticsFocus, 0f..1f, default = d.cymaticsFocus) { onChange(p.copy(cymaticsFocus = it)) }
        ParamKeys.PLATE_RING -> LabeledSlider(key, p.cymaticsRing, 0f..1f, default = d.cymaticsRing) { onChange(p.copy(cymaticsRing = it)) }
        ParamKeys.FIELD_SCALE ->
            LabeledSlider(key, p.cymaticsScale, 0.5f..8f, default = d.cymaticsScale) { onChange(p.copy(cymaticsScale = it)) }
        ParamKeys.WAVE_FLOW -> LabeledSlider(key, p.cymaticsFlow, 0f..1f, default = d.cymaticsFlow) { onChange(p.copy(cymaticsFlow = it)) }
        ParamKeys.FIELD_SWIRL ->
            LabeledSlider(key, p.cymaticsSwirl, -1f..1f, default = d.cymaticsSwirl) { onChange(p.copy(cymaticsSwirl = it)) }
        ParamKeys.FILL -> LabeledSlider(key, p.cymaticsFill, 0f..1f, default = d.cymaticsFill) { onChange(p.copy(cymaticsFill = it)) }
        ParamKeys.NODAL_LINES ->
            LabeledSlider(key, p.cymaticsLine, 0f..2f, display = "Still lines", default = d.cymaticsLine) {
                onChange(p.copy(cymaticsLine = it))
            }
        ParamKeys.NODAL_GLOW ->
            LabeledSlider(key, p.cymaticsGlow, 0f..2f, display = "Line glow", default = d.cymaticsGlow) {
                onChange(p.copy(cymaticsGlow = it))
            }
        ParamKeys.IRIDESCENCE ->
            LabeledSlider(key, p.cymaticsIridescence, 0f..1f, display = "Rainbow sheen", default = d.cymaticsIridescence) {
                onChange(p.copy(cymaticsIridescence = it))
            }
        ParamKeys.CAUSTIC_SHEEN ->
            LabeledSlider(key, p.cymaticsCaustic, 0f..1.5f, display = "Light ripples", default = d.cymaticsCaustic) {
                onChange(p.copy(cymaticsCaustic = it))
            }
        ParamKeys.BLEND_PRESET_CHANGES -> CheckRow(key, p.milkdropBlendPresets) { onChange(p.copy(milkdropBlendPresets = it)) }
        ParamKeys.MILKDROP_PALETTE_TINT ->
            LabeledSlider(key, p.milkdropPaletteTint, 0f..1f, display = "Tint toward the palette", default = d.milkdropPaletteTint) {
                onChange(p.copy(milkdropPaletteTint = it))
            }
        // A name the map has and this panel does not draw yet: nothing, rather than a wrong widget.
        else -> Unit
    }
}

/**
 * A panel piece that is not one parameter (`@id` in core/viz/CustomizeMap): the macro knobs, the
 * moment rows, the route editor. In a Pro section a free user sees it and a tap opens the paywall
 * ([ProGated] by its id); in a Basics section it is free like the rest.
 */
@Composable
private fun PanelPiece(
    id: String,
    host: AdvancedHost,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val context = LocalContext.current
    val vm = host.visualsViewModel
    val viz by host.viewModel.vizState.collectAsStateWithLifecycle()
    when (id) {
        "@macros" -> {
            val store = remember { MacroStore.get(context) }
            val macros by store.state.collectAsStateWithLifecycle()
            MacrosTab(macros, store)
        }
        "@macro_sets" -> {
            val store = remember { MacroStore.get(context) }
            val macros by store.state.collectAsStateWithLifecycle()
            val lock = LocalProLock.current
            MacroSetsPage(macros, store, canSavePresets = lock.unlocked, onLockedSave = lock.onLocked)
        }
        "@randomize_reset" -> {
            val store = remember { MacroStore.get(context) }
            val sounds = LocalUiSounds.current
            FlowRow(horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
                FilledTonalButton(onClick = {
                    sounds.play(UiSound.TAP)
                    store.surprise()
                }) { Text("Surprise me") }
                FilledTonalButton(onClick = {
                    sounds.play(UiSound.TAP)
                    store.resetKnobs()
                }) { Text("Reset the knobs") }
            }
        }
        "@palette_maker" -> ProGated(id, "Make a palette") { if (sectionVisible()) PaletteMakerCard(p, onChange, rememberSavedPalettes()) }
        "@auto_moments" -> DirectorSwitch()
        "@moments" -> {
            val reactions by vm.reactions.collectAsStateWithLifecycle()
            val handMask by vm.handMask.collectAsStateWithLifecycle()
            ProGated(id, "Moments") {
                EventsTab(
                    p,
                    hand = HandReactions(reactions, handMask),
                    onReactionsChange = vm::setReactions,
                    onHandChange = vm::setHandReactions,
                )
            }
        }
        "@rise_fall" -> RiseAndFall(viz.attack, viz.decay, host.viewModel::setReactivity)
        "@notes_key" -> NoteKeyPage(p, onChange)
        "@transition_style" -> TransitionPicker(viz.transitionId, host.viewModel::setTransitionId)
        "@transition_length" ->
            if (viz.transitionId != null) {
                LabeledSlider(
                    TRANSITION_LENGTH,
                    viz.transitionDurationSec,
                    0.3f..5f,
                    scope = ParamScope.UNIVERSAL,
                    unit = ParamUnit.SECONDS,
                ) { host.viewModel.setTransitionDuration(it) }
            }
        "@reactions" -> {
            val reactions by vm.reactions.collectAsStateWithLifecycle()
            val handMask by vm.handMask.collectAsStateWithLifecycle()
            ProGated(id, "Reactions") { ReactionsSection(p, reactions, vm::setReactions, vm::rerollReactions, handMask) }
        }
        "@envelopes" -> {
            val adsrs by vm.adsrs.collectAsStateWithLifecycle()
            if (sectionVisible()) {
                ProGated(id, "Envelopes") {
                    Column {
                        for (i in 0 until AdsrEngine.COUNT) {
                            AdsrCard(index = i, config = adsrs.getOrElse(i) { AdsrConfig() }, onChange = { vm.setAdsr(i, it) })
                        }
                    }
                }
            }
        }
        "@waves" -> {
            val lfos by vm.lfos.collectAsStateWithLifecycle()
            if (sectionVisible()) {
                ProGated(id, "Waves") {
                    Column {
                        for (i in 0 until LfoEngine.SLOTS) {
                            ModulatorCard(index = i, config = lfos.getOrElse(i) { LfoConfig() }, onChange = { vm.setLfo(i, it) })
                        }
                    }
                }
            }
        }
        "@layers" -> ProGated(id, "Layers") { LayersPage() }
        "@shader_code" -> if (sectionVisible()) ProGated(id, "Shader code") { ShaderCodePage(host) }
        else -> Unit
    }
}

/** Look's palette, with "Take the colours from the artwork" over it (the same free control). */
@Composable
private fun PaletteControl(
    host: AdvancedHost,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val artNote by host.viewModel.artPaletteNote.collectAsStateWithLifecycle()
    if (sectionVisible()) {
        ProGated(ParamKeys.PALETTE, "Take the colours from the artwork") {
            val sounds = LocalUiSounds.current
            FilledTonalButton(onClick = {
                sounds.play(UiSound.TAP)
                host.viewModel.applyArtworkPalette()
            }) { Text("Take the colours from the artwork") }
        }
        artNote?.let { ControlHint(it) }
    }
    if (visible(ParamKeys.PALETTE, ParamScope.UNIVERSAL)) {
        ProGated(ParamKeys.PALETTE) {
            PaletteDropdown(ParamKeys.PALETTE, ParamKeys.PALETTE, p, onChange, rememberSavedPalettes(), second = false)
        }
    }
}

@Composable
private fun KaleidoscopeControl(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    CheckRow(ParamKeys.KALEIDOSCOPE, p.kaleidoscope) { on ->
        val folds = if (on && p.symmetry < 2) SceneParams.DEFAULT_SYMMETRY_FOLDS else p.symmetry
        onChange(p.copy(kaleidoscope = on, symmetry = folds))
    }
    if (p.kaleidoscope && visible(ParamKeys.KALEIDOSCOPE, ParamScope.of(ParamKeys.KALEIDOSCOPE))) {
        val folds = SceneParams.SYMMETRY_FOLDS.filter { it >= 2 }
        ProGated(ParamKeys.KALEIDOSCOPE, "Folds", "${p.symmetry}") {
            DropdownRow("Folds", folds.map { "$it" }, selectedIndex = folds.indexOf(p.symmetry)) { idx ->
                onChange(p.copy(symmetry = folds[idx]))
            }
        }
    }
}

@Composable
private fun CymaticsGeometry(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val sceneId = LocalSceneId.current
    if (!SceneCapabilities.isCymatics(sceneId)) return
    val forced = Catalogue.override(sceneId, ParamKeys.GEOMETRY)?.let { SceneParams.CYMATICS_GEOMETRIES.getOrNull(it.toInt()) }
    if (forced != null) {
        ControlHint("Shape set by this style: $forced.", ParamScope.CYMATICS)
    } else {
        ParamChips(ParamKeys.GEOMETRY, SceneParams.CYMATICS_GEOMETRIES, p.cymaticsGeometry) { onChange(p.copy(cymaticsGeometry = it)) }
    }
}

/** Liquid's switch, with the hot-phone note under it when the renderer has simplified or paused it. */
@Composable
private fun LiquidSwitch(
    host: AdvancedHost,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val liquidMode by host.visualsViewModel.liquidMode.collectAsStateWithLifecycle()
    CheckRow(LIQUID_ON, p.flowEnabled, display = "Liquid mode", scope = ParamScope.UNIVERSAL) { onChange(p.copy(flowEnabled = it)) }
    liquidHotNote(liquidMode)?.let { note ->
        Text(
            note,
            modifier = Modifier.padding(top = 4.dp, bottom = 2.dp),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.error,
        )
    }
}

/**
 * Behaviour's Personality: a new reaction DNA for the style (core/viz/ReactionDna). The same
 * shuffle the reaction editor offers; the user's own routes go back to the style's.
 */
@Composable
private fun PersonalityControl(host: AdvancedHost) {
    if (!visible(REACTION_DNA, ParamScope.UNIVERSAL, "Personality")) return
    val sounds = LocalUiSounds.current
    ControlLabelRow("Personality", REACTION_DNA)
    ProGated(REACTION_DNA, "Personality") {
        FilledTonalButton(onClick = {
            sounds.play(UiSound.TAP)
            host.visualsViewModel.rerollReactions()
        }) { Text("New personality") }
    }
}

/** Music › Listening's rise and fall: how fast the picture follows a sound up and lets it go. */
@Composable
private fun RiseAndFall(
    attack: Float,
    decay: Float,
    onReactivityChange: (Float, Float) -> Unit,
) {
    LabeledSlider(RISE, attack, 0.05f..1f, scope = ParamScope.BAND_GAINS) { onReactivityChange(it, decay) }
    LabeledSlider(FALL, decay, 0.02f..0.6f, scope = ParamScope.BAND_GAINS) { onReactivityChange(attack, it) }
}

/**
 * Music's Auto moments: the auto-director, the same switch (and store) as Settings › Behaviour ›
 * Auto-director and Visuals' quick settings, so the three never disagree.
 */
@Composable
private fun DirectorSwitch() {
    val label = stringResource(R.string.customize_director)
    if (!visible(label, ParamScope.UNIVERSAL)) return
    val context = LocalContext.current
    val store = remember { AutoDirectorStore.get(context) }
    val on by store.enabled.collectAsStateWithLifecycle()
    LabeledSwitch(label, checked = on, onCheckedChange = store::setEnabled)
}

/** Above this the second palette (or a water film) is in the picture, so its controls show. */
private const val PALETTE_MIX_SHOWN = 0.001f

// Names of panel controls that are not ParamKeys, as core/viz/CustomizeMap spells them.
internal const val REACTION_DNA = "Reaction DNA"
private const val PROGRESSION = "Progression"
private const val FLUID_QUALITY = "Fluid quality"
private const val AUTO_QUALITY = "Lower the quality when the phone struggles"
private const val PARTICLE_LAYER = "Particle layer"
private const val INK_LAYER = "Ink layer"
private const val LIQUID_ON = "Flow field on"
private const val RIPPLES_ON = "Water ripples on"
private const val GLIDE_TIME = "Glide time"
private const val TRANSITION_LENGTH = "Transition length"
private const val RISE = "Rise"
private const val FALL = "Fall"

private val LIQUID_BORN_CHOICES = listOf("Live style", "Style re-rolled", "Next style", "Forms")
private val LIQUID_AGE_CHOICES = listOf("Pure palette", "Fade deep", "Ink bleed")
private val LIQUID_SURFACE_CHOICES = listOf("Ink", "Gloss", "Glass")
