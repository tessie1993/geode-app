package dev.geode.ui

import androidx.compose.foundation.layout.Column
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
import dev.geode.data.VizUiState
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import dev.geode.model.ControlRange
import dev.geode.model.int
import dev.geode.model.with
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.ParamUnit
import dev.geode.render.scene.SceneParams
import dev.geode.render.scene.StyleCatalogue
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
internal fun CustomizeControls(
    keys: List<String>,
    host: AdvancedHost,
) {
    // One collector for the whole section, not one per control: each control used to subscribe to
    // the full VizUiState itself, so a slider drag woke every collector in every open section.
    val viz by host.viewModel.vizState.collectAsStateWithLifecycle()
    keys.forEach { CustomizeControl(it, host, viz) }
}

@Composable
private fun CustomizeControl(
    key: String,
    host: AdvancedHost,
    viz: VizUiState,
) {
    val p = viz.params
    val onChange: (SceneParams) -> Unit = host.visualsViewModel::editSceneParams
    if (key.startsWith("@")) {
        PanelPiece(key, host, viz, onChange)
        return
    }
    // Each group draws only the keys it knows, so where the native map files a control never
    // decides whether it draws.
    LookControl(key, host, p, onChange)
    ShapeControl(key, p, onChange)
    MotionControl(key, p, onChange)
    MusicControl(key, p, onChange)
    LiquidControl(key, host, p, onChange)
    EngineControl(key, p, onChange)
}

@Composable
private fun LookControl(
    key: String,
    host: AdvancedHost,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    when (key) {
        ParamKeys.PALETTE -> PaletteControl(host, p, onChange)
        ParamKeys.BRIGHTNESS ->
            // Upper ends are the engine's flash clamp (1 + `kMaxFlashDepth` in core/viz/VisualSafety.hpp).
            LabeledSlider(key, p, "brightness", onChange = onChange)
        ParamKeys.SATURATION -> LabeledSlider(key, p, "saturation", onChange = onChange)
        ParamKeys.BLOOM -> LabeledSlider(key, p, "bloom", display = "Glow", onChange = onChange)
        ParamKeys.PALETTE_2 ->
            if (p.paletteMix > PALETTE_MIX_SHOWN && visible(key, ParamScope.SHADER_LOOK)) {
                ProGated(key) { SecondPaletteStepper(p, onChange) }
            }
        ParamKeys.PALETTE_BLEND -> LabeledSlider(key, p, "paletteMix", onChange = onChange)
        ParamKeys.COLOUR_MAP ->
            if (visible(key, ParamScope.SHADER_LOOK)) {
                val maps = remember { colourMapNames() }
                val selected = maps.getOrNull(p.paletteLut) ?: "Off"
                ProGated(key, value = selected) {
                    DropdownRow(
                        name = key,
                        options = listOf("Off") + maps,
                        selectedIndex = if (p.paletteLut < 0) 0 else p.paletteLut + 1,
                        lockKey = key,
                        about = "Even colour steps that loop, so a wrap has no seam.",
                    ) { i -> onChange(p.copy(paletteLut = if (i == 0) SceneParams.NO_PALETTE_LUT else i - 1)) }
                }
            }
        ParamKeys.HUE_SHIFT -> LabeledSlider(key, p, "colorShift", onChange = onChange)
        ParamKeys.HUE_RANGE -> LabeledSlider(key, p, "hueRange", onChange = onChange)
        ParamKeys.TEMPERATURE -> LabeledSlider(key, p, "temperature", display = "Warmth", onChange = onChange)
        ParamKeys.COLOR_CYCLE -> CheckRow(key, p.colorCycle, display = "Cycle the colours") { onChange(p.copy(colorCycle = it)) }
        ParamKeys.CYCLE_SPEED ->
            if (p.colorCycle) {
                LabeledSlider(key, p, "cycleSpeed", onChange = onChange)
            }
        ParamKeys.CONTRAST -> LabeledSlider(key, p, "contrast", onChange = onChange)
        ParamKeys.GAMMA -> LabeledSlider(key, p, "gamma", display = "Midtones", onChange = onChange)
        ParamKeys.INTENSITY -> LabeledSlider(key, p, "intensity", onChange = onChange)
        else -> Unit
    }
}

@Composable
private fun ShapeControl(
    key: String,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    when (key) {
        ParamKeys.MORPH -> LabeledSlider(key, p, "morph", onChange = onChange)
        ParamKeys.KALEIDOSCOPE -> KaleidoscopeControl(p, onChange)
        ParamKeys.MARCH_DETAIL -> LabeledSlider(key, p, "marchDetail", onChange = onChange)
        ParamKeys.DOMAIN_WARP -> LabeledSlider(key, p, "warp", display = "Warp", onChange = onChange)
        ParamKeys.TWIST -> LabeledSlider(key, p, "twist", onChange = onChange)
        ParamKeys.RIPPLE -> LabeledSlider(key, p, "ripple", onChange = onChange)
        ParamKeys.TURBULENCE -> LabeledSlider(key, p, "turbulence", onChange = onChange)
        ParamKeys.MIRROR -> CheckRow(key, p.mirror) { onChange(p.copy(mirror = it)) }
        ParamKeys.TILE -> LabeledSlider(key, p, "tile", onChange = onChange)
        ParamKeys.PIXELATE -> LabeledSlider(key, p, "pixelate", onChange = onChange)
        ParamKeys.PARTICLE_SHAPE -> ChoiceChips(key, "particleShape", p, onChange = onChange)
        ParamKeys.PARTICLE_SIZE -> LabeledSlider(key, p, "particleSize", onChange = onChange)
        ParamKeys.DENSITY -> LabeledSlider(key, p, "density", onChange = onChange)
        ParamKeys.PARTICLE_LIFE_S -> LabeledSlider(key, p, "fluidParticleLife", onChange = onChange)
        ParamKeys.PARTICLE_DRAG -> LabeledSlider(key, p, "fluidParticleDrag", onChange = onChange)
        ParamKeys.PATH -> ChoiceChips(key, "fluidSpawnPath", p, onChange = onChange)
        ParamKeys.SPAWN_POINTS -> LabeledIntSlider(key, p, "fluidSpawnPoints", onChange = onChange)
        PROGRESSION -> LabeledSlider(key, p, "fluidSpawnProgress", scope = ParamScope.JOURNEY, onChange = onChange)
        ParamKeys.CATCH_POINTS -> LabeledIntSlider(key, p, "fluidCatchPoints", onChange = onChange)
        ParamKeys.CATCH_PULL ->
            if (p.fluidCatchPoints > 0) {
                LabeledSlider(key, p, "fluidCatchPull", onChange = onChange)
            }
        ParamKeys.CATCH_RADIUS ->
            if (p.fluidCatchPoints > 0) {
                LabeledSlider(key, p, "fluidCatchRadius", onChange = onChange)
            }
        ParamKeys.BEAT_PATTERN -> ChoiceChips(key, "fluidBeatPattern", p, onChange = onChange)
        ParamKeys.BEAT_SPLATS -> LabeledIntSlider(key, p, "fluidBeatSplats", onChange = onChange)
        ParamKeys.STIRRERS -> LabeledIntSlider(key, p, "fluidStirrers", onChange = onChange)
        ParamKeys.STIRRER_SPEED -> LabeledSlider(key, p, "fluidStirrerSpeed", onChange = onChange)
        ParamKeys.FLUID_SPLAT_RADIUS -> LabeledSlider(key, p, "fluidSplatRadius", onChange = onChange)
        ParamKeys.RADIUS_ON_BEAT -> LabeledSlider(key, p, "fluidRadiusPulse", onChange = onChange)
        ParamKeys.FLUID_SPLAT_FORCE -> LabeledSlider(key, p, "fluidSplatForce", onChange = onChange)
        ParamKeys.BASS_PUMP -> CheckRow(key, p.fluidBassPump) { onChange(p.copy(fluidBassPump = it)) }
        ParamKeys.TREBLE_SPARKLE -> CheckRow(key, p.fluidSparkle) { onChange(p.copy(fluidSparkle = it)) }
        FLUID_QUALITY ->
            if (visible(key, ParamScope.EMITTERS)) {
                val labels = ParamSchema.choices("fluidQuality")
                val quality = p.fluidQuality.coerceIn(0, maxOf(labels.lastIndex, 0))
                ProGated(key, value = labels.getOrNull(quality)) {
                    DropdownRow(key, labels, quality) { onChange(p.copy(fluidQuality = it)) }
                }
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
    when (key) {
        ParamKeys.SPEED -> LabeledSlider(key, p, "speed", onChange = onChange)
        ParamKeys.ZOOM -> LabeledSlider(key, p, "zoom", onChange = onChange)
        ParamKeys.ROTATION -> LabeledSlider(key, p, "rotation", display = "Spin", onChange = onChange)
        ParamKeys.SWAY -> LabeledSlider(key, p, "sway", onChange = onChange)
        ParamKeys.DRIFT_X -> LabeledSlider(key, p, "driftX", display = "Drift sideways", onChange = onChange)
        ParamKeys.DRIFT_Y -> LabeledSlider(key, p, "driftY", display = "Drift up and down", onChange = onChange)
        ParamKeys.ENDLESS_ZOOM -> CheckRow(key, p.endlessZoom) { onChange(p.copy(endlessZoom = it)) }
        ParamKeys.DIVE_SPEED ->
            // Liquid mode dives on any style, so Dive speed shows with it as well as with the endless zoom.
            if (p.endlessZoom || p.flowEnabled) {
                val scope = if (p.flowEnabled) ParamScope.UNIVERSAL else ParamScope.ENDLESS_ZOOM
                LabeledSlider(key, p, "endlessZoomSpeed", scope = scope, onChange = onChange)
            }
        DIVE_SPACING -> LabeledSlider(key, p, "diveSpacing", onChange = onChange)
        DIVE_SHAPE -> LabeledSlider(key, p, "diveShape", onChange = onChange)
        DIVE_BLEND -> LabeledSlider(key, p, "diveBlend", display = "Soft edges", onChange = onChange)
        DIVE_FOG -> LabeledSlider(key, p, "diveFog", onChange = onChange)
        DIVE_TWIST -> LabeledSlider(key, p, "diveTwist", display = "Spiral", onChange = onChange)
        DIVE_AIM_X -> LabeledSlider(key, p, "diveAimX", display = "Aim sideways", onChange = onChange)
        DIVE_AIM_Y -> LabeledSlider(key, p, "diveAimY", display = "Aim up and down", onChange = onChange)
        DIVE_LENS -> LabeledSlider(key, p, "diveLens", display = "Lens", onChange = onChange)
        DIVE_FOLLOW -> LabeledSlider(key, p, "diveFollow", onChange = onChange)
        ParamKeys.MOTION_AMOUNT -> LabeledSlider(key, p, "motionAmount", onChange = onChange)
        ParamKeys.MOTION_BREATH -> LabeledSlider(key, p, "motionBreath", onChange = onChange)
        ParamKeys.SPIN -> LabeledSlider(key, p, "motionDrift", onChange = onChange)
        ParamKeys.CAM_PATH -> ChoiceChips(key, "camPath", p, ParamScope.UNIVERSAL, onChange = onChange)
        // Below zero the camera travels backward, and a flat style zooms out instead of in.
        ParamKeys.CAM_FLY -> LabeledSlider(key, p, "camFly", onChange = onChange)
        ParamKeys.CAM_FLAT -> LabeledSlider(key, p, "camFlat", onChange = onChange)
        ParamKeys.CAM_FOV -> LabeledSlider(key, p, "camFov", onChange = onChange)
        ParamKeys.CAM_ORBIT -> LabeledSlider(key, p, "camOrbit", onChange = onChange)
        ParamKeys.CAM_PUSH -> LabeledSlider(key, p, "camPush", onChange = onChange)
        ParamKeys.CAM_YAW -> LabeledSlider(key, p, "camYaw", onChange = onChange)
        ParamKeys.CAM_PITCH -> LabeledSlider(key, p, "camPitch", onChange = onChange)
        ParamKeys.CAM_ROLL -> LabeledSlider(key, p, "camRoll", onChange = onChange)
        ParamKeys.CAM_SHAKE -> LabeledSlider(key, p, "camShake", onChange = onChange)
        ParamKeys.CAM_GYRO -> CheckRow(key, p.camGyro) { onChange(p.copy(camGyro = it)) }
        // One finger never reaches the visual (ui/VisualGestures); the look follows a two-finger drag.
        ParamKeys.CAM_TOUCH -> CheckRow(key, p.camTouch, display = "Two-finger drag to look") { onChange(p.copy(camTouch = it)) }
        ParamKeys.CAM_SMOOTH -> LabeledSlider(key, p, "camSmooth", onChange = onChange)
        else -> Unit
    }
}

@Composable
private fun MusicControl(
    key: String,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    when (key) {
        ParamKeys.AUDIO_DRIVE -> LabeledSlider(key, p, "audioDrive", display = "Sound strength", onChange = onChange)
        ParamKeys.REACT_AMOUNT -> LabeledSlider(key, p, "reactAmount", onChange = onChange)
        ParamKeys.BASS_GAIN -> LabeledSlider(key, p, "bassGain", display = "Bass", onChange = onChange)
        ParamKeys.MID_GAIN -> LabeledSlider(key, p, "midGain", display = "Mids", onChange = onChange)
        ParamKeys.TREBLE_GAIN -> LabeledSlider(key, p, "trebGain", display = "Highs", onChange = onChange)
        ParamKeys.MOTION_HUE -> LabeledSlider(key, p, "motionHue", onChange = onChange)
        ParamKeys.TRAILS -> CheckRow(key, p.trails) { onChange(p.copy(trails = it)) }
        ParamKeys.VIGNETTE -> LabeledSlider(key, p, "vignette", display = "Dark edges", onChange = onChange)
        ParamKeys.TRAIL_LENGTH -> LabeledSlider(key, p, "trailLength", onChange = onChange)
        ParamKeys.TRAIL_ZOOM_ECHO_IN_OUT -> LabeledSlider(key, p, "trailZoom", display = "Trail zoom", onChange = onChange)
        ParamKeys.TRAIL_WARP_LIQUID_ECHO -> LabeledSlider(key, p, "trailWarp", display = "Trail ripple", onChange = onChange)
        ParamKeys.CHROMATIC_ABERRATION -> LabeledSlider(key, p, "chromaAb", display = "Colour fringe", onChange = onChange)
        ParamKeys.FISHEYE -> LabeledSlider(key, p, "fisheye", display = "Bulge", onChange = onChange)
        ParamKeys.FILM_GRAIN -> LabeledSlider(key, p, "grain", onChange = onChange)
        ParamKeys.SCANLINES -> LabeledSlider(key, p, "scanlines", onChange = onChange)
        ParamKeys.POSTERIZE -> LabeledSlider(key, p, "posterize", display = "Fewer colours", onChange = onChange)
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
    when (key) {
        LIQUID_ON -> LiquidSwitch(host, p, onChange)
        ParamKeys.FLOW_STRENGTH ->
            if (p.flowEnabled) {
                LabeledSlider(key, p, "flowStrength", display = "Liquidity", scope = ParamScope.UNIVERSAL, onChange = onChange)
            }
        ParamKeys.FLOW_FORCE ->
            if (p.flowEnabled) {
                LabeledSlider(key, p, "flowForce", display = "Push", scope = ParamScope.UNIVERSAL, onChange = onChange)
            }
        ParamKeys.FLOW_CURL ->
            if (p.flowEnabled) {
                LabeledSlider(key, p, "flowCurl", display = "Swirl", scope = ParamScope.UNIVERSAL, onChange = onChange)
            }
        ParamKeys.LIQUID_BORN -> ChoiceChips(key, "liquidBorn", p, ParamScope.UNIVERSAL, onChange = onChange)
        ParamKeys.LIQUID_AGE -> ChoiceChips(key, "liquidAge", p, ParamScope.UNIVERSAL, onChange = onChange)
        ParamKeys.LIQUID_SURFACE -> ChoiceChips(key, "liquidSurface", p, ParamScope.UNIVERSAL, onChange = onChange)
        REACTION_DNA -> PersonalityControl(host)
        GLIDE_TIME -> LabeledSlider(key, p, "paramFadeSec", scope = ParamScope.UNIVERSAL, unit = ParamUnit.SECONDS, onChange = onChange)
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
    val sceneId = LocalSceneId.current
    when (key) {
        ParamKeys.SOLVER_ITERATIONS -> LabeledIntSlider(key, p, "fluidIterations", display = "Accuracy", onChange = onChange)
        ParamKeys.FLUID_CURL ->
            if (StyleCatalogue.isFluid(sceneId)) {
                LabeledSlider(key, p, "fluidCurl", display = "Fluid swirl", onChange = onChange)
            }
        ParamKeys.MOTION_FADE -> LabeledSlider(key, p, "fluidVelocityDissipation", onChange = onChange)
        ParamKeys.FLUID_FADE -> LabeledSlider(key, p, "fluidDensityDissipation", onChange = onChange)
        ParamKeys.CHROMATIC_AGING -> LabeledSlider(key, p, "fluidChromaticAging", display = "Colour ageing", onChange = onChange)
        ParamKeys.PRESSURE -> LabeledSlider(key, p, "fluidPressure", onChange = onChange)
        ParamKeys.SHADING_EMBOSSED_INK -> CheckRow(key, p.fluidShading, display = "Embossed ink") { onChange(p.copy(fluidShading = it)) }
        ParamKeys.GLOW_FLUID -> CheckRow(key, p.fluidBloom, display = "Fluid glow") { onChange(p.copy(fluidBloom = it)) }
        ParamKeys.FLUID_GLOW ->
            if (p.fluidBloom) {
                LabeledSlider(key, p, "fluidBloomIntensity", display = "Glow amount", onChange = onChange)
            }
        ParamKeys.GLOW_THRESHOLD ->
            if (p.fluidBloom) {
                LabeledSlider(key, p, "fluidBloomThreshold", onChange = onChange)
            }
        ParamKeys.SUNRAYS -> CheckRow(key, p.fluidSunrays) { onChange(p.copy(fluidSunrays = it)) }
        ParamKeys.SUNRAYS_WEIGHT ->
            if (p.fluidSunrays) {
                LabeledSlider(key, p, "fluidSunraysWeight", display = "Sunray strength", onChange = onChange)
            }
        ParamKeys.CURL_FROM_MIDS -> LabeledSlider(key, p, "fluidCurlAudio", display = "Swirl from the mids", onChange = onChange)
        ParamKeys.GLOW_FROM_LOUDNESS -> LabeledSlider(key, p, "fluidBloomAudio", display = "Glow from volume", onChange = onChange)
        ParamKeys.FADE_WHEN_QUIET -> LabeledSlider(key, p, "fluidFadeAudio", onChange = onChange)
        ParamKeys.PALETTE_CYCLE -> LabeledSlider(key, p, "fluidPaletteCycleSpeed", onChange = onChange)
        ParamKeys.PARTICLE_BRIGHTNESS -> LabeledSlider(key, p, "fluidParticleBrightness", onChange = onChange)
        PARTICLE_LAYER ->
            CheckRow(key, p.fluidParticlesEnabled, scope = ParamScope.FLUID_SIM) { onChange(p.copy(fluidParticlesEnabled = it)) }
        INK_LAYER -> CheckRow(key, p.fluidDyeEnabled, scope = ParamScope.FLUID_SIM) { onChange(p.copy(fluidDyeEnabled = it)) }
        ParamKeys.GEOMETRY -> CymaticsGeometry(p, onChange)
        ParamKeys.FUNDAMENTAL_HZ ->
            LabeledSlider(key, p.cymaticsFundamental, ControlRange.of("cymaticsFundamental"), display = "Base note (Hz)") {
                onChange(p.copy(cymaticsFundamental = it))
            }
        ParamKeys.STANDING_WAVES -> LabeledIntSlider(key, p, "cymaticsModes", onChange = onChange)
        ParamKeys.TONAL_FOCUS -> LabeledSlider(key, p, "cymaticsFocus", onChange = onChange)
        ParamKeys.PLATE_RING -> LabeledSlider(key, p, "cymaticsRing", onChange = onChange)
        ParamKeys.FIELD_SCALE -> LabeledSlider(key, p, "cymaticsScale", onChange = onChange)
        ParamKeys.WAVE_FLOW -> LabeledSlider(key, p, "cymaticsFlow", onChange = onChange)
        ParamKeys.FIELD_SWIRL -> LabeledSlider(key, p, "cymaticsSwirl", onChange = onChange)
        ParamKeys.FILL -> LabeledSlider(key, p, "cymaticsFill", onChange = onChange)
        ParamKeys.NODAL_LINES -> LabeledSlider(key, p, "cymaticsLine", display = "Still lines", onChange = onChange)
        ParamKeys.NODAL_GLOW -> LabeledSlider(key, p, "cymaticsGlow", display = "Line glow", onChange = onChange)
        ParamKeys.IRIDESCENCE -> LabeledSlider(key, p, "cymaticsIridescence", display = "Rainbow sheen", onChange = onChange)
        ParamKeys.CAUSTIC_SHEEN -> LabeledSlider(key, p, "cymaticsCaustic", display = "Light ripples", onChange = onChange)
        ParamKeys.BLEND_PRESET_CHANGES -> CheckRow(key, p.milkdropBlendPresets) { onChange(p.copy(milkdropBlendPresets = it)) }
        ParamKeys.MILKDROP_PALETTE_TINT ->
            LabeledSlider(key, p, "milkdropPaletteTint", display = "Tint toward the palette", onChange = onChange)
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
    viz: VizUiState,
    onChange: (SceneParams) -> Unit,
) {
    val context = LocalContext.current
    val vm = host.visualsViewModel
    val p = viz.params
    when (id) {
        "@macros" -> {
            val store = remember { context.geodeContainer.macros }
            val macros by store.state.collectAsStateWithLifecycle()
            MacrosTab(macros, store, viz.sceneId, LocalProLock.current)
        }
        "@macro_editor" -> {
            val store = remember { context.geodeContainer.macros }
            val macros by store.state.collectAsStateWithLifecycle()
            ProGated(id, stringResource(R.string.macro_editor_title)) { MacroEditor(macros, store, viz.sceneId) }
        }
        "@palette_maker" ->
            if (sectionVisible()) {
                ProGated(
                    id,
                    stringResource(R.string.customize_your_colours),
                ) { YourColoursCard(host, p, onChange, rememberSavedPalettes()) }
            }
        "@moments" -> {
            val reactions by vm.reactions.collectAsStateWithLifecycle()
            val handMask by vm.handMask.collectAsStateWithLifecycle()
            ProGated(id, "Moments") { EventsTab(p, reactions, handMask, vm::editReactions) }
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
            ProGated(id, "Reactions") { ReactionsSection(p, reactions, handMask, vm::editReactions, vm::rerollReactions) }
        }
        "@envelopes" -> {
            val adsrs by vm.adsrs.collectAsStateWithLifecycle()
            if (sectionVisible()) {
                ProGated(id, "Envelopes") {
                    Column {
                        for (i in 0 until ModCatalog.count("envelopes")) {
                            AdsrCard(index = i, adsrs = adsrs, onChange = { field, value -> vm.setAdsr(i, field, value) })
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
                        val sounds = LocalUiSounds.current
                        FilledTonalButton(onClick = {
                            sounds.play(UiSound.TAP)
                            vm.randomizeWaves()
                        }) { Text("Randomize waves") }
                        for (i in 0 until ModCatalog.count("waves")) {
                            ModulatorCard(index = i, lfos = lfos, onChange = { field, value -> vm.setLfo(i, field, value) })
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
            }) { Text(stringResource(R.string.customize_palette_from_artwork)) }
        }
        artNote?.let { ControlHint(it) }
    }
    if (visible(ParamKeys.PALETTE, ParamScope.UNIVERSAL)) {
        ProGated(ParamKeys.PALETTE) {
            PaletteDropdown(ParamKeys.PALETTE, p, onChange, rememberSavedPalettes())
        }
        PaletteStackCard(p, onChange)
    }
}

/**
 * Kaleidoscope as one slider of whole folds over the schema's range, 1 to 16: native words the value ("Off" at 1,
 * else the count) and rounds it for the shaders, and a knob, a wave or a route moves it like any slider.
 */
@Composable
private fun KaleidoscopeControl(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val range = ControlRange.of(KALEIDO_FOLDS)
    val steps = remember(range) { GeodeNative.sliderIntSteps(range.start.toInt(), range.endInclusive.toInt()) }
    LabeledSlider(ParamKeys.KALEIDOSCOPE, p, KALEIDO_FOLDS, steps = steps, onChange = onChange)
}

@Composable
private fun CymaticsGeometry(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val sceneId = LocalSceneId.current
    if (!StyleCatalogue.isCymatics(sceneId)) return
    val shapes = ParamSchema.choices("cymaticsGeometry")
    val forced = StyleCatalogue.held(sceneId, ParamKeys.GEOMETRY)?.let { shapes.getOrNull(it.toInt()) }
    if (forced != null) {
        ControlHint("Shape set by this style: $forced.", ParamScope.CYMATICS)
    } else {
        ChoiceChips(ParamKeys.GEOMETRY, "cymaticsGeometry", p, onChange = onChange)
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
 * Music's Personality: a new reaction DNA for the style (core/viz/ReactionDna). The same
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
        }) { Text(stringResource(R.string.customize_new_personality)) }
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

/** A picker as [ParamChips] for the scene param named [name], its labels native's (the schema's choice rows); none without them. */
@Composable
private fun ChoiceChips(
    key: String,
    name: String,
    p: SceneParams,
    scope: ParamScope = ParamScope.of(key),
    onChange: (SceneParams) -> Unit,
) {
    val labels = ParamSchema.choices(name)
    if (labels.isEmpty()) return
    ParamChips(key, labels, p.int(name).coerceIn(0, labels.lastIndex), scope) { onChange(p.with(name, it.toFloat())) }
}

/** Above this the second palette is in the picture, so its controls show. */
private const val PALETTE_MIX_SHOWN = 0.001f

/** The Kaleidoscope slider's scene param (core/viz/ParamTable.inc). */
private const val KALEIDO_FOLDS = "kaleidoFolds"

/** The colour maps' names in `paletteLut` order, from the native palette library. */
private fun colourMapNames(): List<String> =
    generateSequence(0) { it + 1 }.map(GeodeNative::paletteColourMapName).takeWhile { it.isNotEmpty() }.toList()

// Names of panel controls that are not ParamKeys, as core/viz/CustomizeMap spells them.
internal const val REACTION_DNA = "Reaction DNA"
private const val PROGRESSION = "Progression"
private const val FLUID_QUALITY = "Fluid quality"
private const val AUTO_QUALITY = "Lower the quality when the phone struggles"
private const val PARTICLE_LAYER = "Particle layer"
private const val INK_LAYER = "Ink layer"
private const val LIQUID_ON = "Flow field on"
private const val DIVE_SPACING = "Ring spacing"
private const val DIVE_SHAPE = "Tunnel shape"
private const val DIVE_BLEND = "Dive blend"
private const val DIVE_FOG = "Depth fade"
private const val DIVE_TWIST = "Dive twist"
private const val DIVE_AIM_X = "Dive aim X"
private const val DIVE_AIM_Y = "Dive aim Y"
private const val DIVE_LENS = "Dive lens"
private const val DIVE_FOLLOW = "Music follow"
private const val GLIDE_TIME = "Glide time"
private const val TRANSITION_LENGTH = "Transition length"
private const val RISE = "Rise"
private const val FALL = "Fall"
