package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.analysis.IntelligenceMode
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.BlendMode
import dev.geode.render.EnvBand
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.LfoTarget
import dev.geode.render.LfoWave
import dev.geode.render.ModCurve
import dev.geode.render.ModPolarity
import dev.geode.render.ModSource
import dev.geode.render.scene.CymaticsMath
import dev.geode.render.scene.MarchBudget
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.ParamRandomizer
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import dev.geode.render.scene.VisualStyleCatalog
import kotlin.math.ln
import kotlin.math.pow

val LocalParamLocks =
    androidx.compose.runtime.compositionLocalOf<Pair<Set<String>, (String) -> Unit>> { emptySet<String>() to {} }

/**
 * The style the panel is editing.
 *
 * Every control asks [ParamScope] whether it can move THIS style and draws nothing if it cannot
 * (R4: hidden, never greyed out and lying). Keeping it in a composition local rather than in each
 * tab's signature is what makes that automatic — a new control is gated the moment it is given a
 * [ParamKeys] name, instead of when somebody remembers to thread another boolean through.
 */
val LocalSceneId = androidx.compose.runtime.compositionLocalOf { SceneIds.DEFAULT }

/** The panel-wide parameter search. Blank shows the tab as authored. */
val LocalParamQuery = androidx.compose.runtime.compositionLocalOf { "" }

/**
 * True when this control both applies to the active style and matches the search.
 *
 * [scope] defaults to the one the parameter's own name declares. It is only passed explicitly for
 * the handful of controls that are a master switch rather than a rolled parameter, and so have no
 * entry of their own in [ParamScope.of].
 */
@Composable
private fun visible(
    paramKey: String,
    scope: ParamScope,
): Boolean {
    if (!scope.appliesTo(LocalSceneId.current)) return false
    val query = LocalParamQuery.current
    return query.isBlank() || paramKey.contains(query.trim(), ignoreCase = true)
}

/** True when a whole section can be drawn: it applies here, and no search is narrowing the panel. */
@Composable
private fun sectionVisible(scope: ParamScope = ParamScope.UNIVERSAL): Boolean =
    LocalParamQuery.current.isBlank() && scope.appliesTo(LocalSceneId.current)

@Composable
private fun SectionHeader(
    title: String,
    scope: ParamScope = ParamScope.UNIVERSAL,
) {
    if (!sectionVisible(scope)) return
    Row(
        modifier = Modifier.fillMaxWidth().padding(top = 14.dp, bottom = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CrystalGem(MaterialTheme.colorScheme.primary, size = 5.dp)
        Text(
            title.uppercase(),
            modifier = Modifier.padding(start = 8.dp),
            style = MaterialTheme.typography.labelMedium.copy(letterSpacing = 2.sp),
            color = accentTextColor(),
        )
    }
    Box(Modifier.fillMaxWidth().height(1.dp).luminousHairline(MaterialTheme.colorScheme.primary.copy(alpha = 0.5f)))
}

@Composable
private fun ControlHint(
    text: String,
    scope: ParamScope = ParamScope.UNIVERSAL,
) {
    if (!sectionVisible(scope)) return
    Text(
        text,
        modifier = Modifier.padding(top = 4.dp, bottom = 2.dp),
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

@Composable
private fun LockChip(label: String) {
    if (label !in ParamRandomizer.LOCKABLE_LABELS) return
    val (locked, toggle) = LocalParamLocks.current
    val on = label in locked
    val text = if (on) "🔒 locked" else "lock"
    Layout(
        content = {
            Text(
                text,
                style = MaterialTheme.typography.labelSmall,
                color = if (on) accentTextColor() else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.45f),
                modifier = Modifier.clearAndSetSemantics {},
            )
            Box(
                modifier =
                    Modifier
                        .semantics { contentDescription = if (on) "$label locked" else "Lock $label" }
                        .clickable(onClickLabel = if (on) "unlock" else "lock") { toggle(label) },
            )
        },
        modifier = Modifier.padding(start = 8.dp),
    ) { measurables, constraints ->
        val pill = measurables[0].measure(constraints)
        val minPx = 48.dp.roundToPx()
        val touch = measurables[1].measure(Constraints.fixed(maxOf(pill.width, minPx), maxOf(pill.height, minPx)))
        layout(pill.width, pill.height) {
            pill.place(0, 0)
            touch.place((pill.width - touch.width) / 2, (pill.height - touch.height) / 2)
        }
    }
}

@Composable
private fun ControlLabelRow(
    text: String,
    lockKey: String,
) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(text, style = MaterialTheme.typography.labelSmall)
        LockChip(lockKey)
    }
}

private const val TRANSITION_CHIP_LIMIT = 40

@Composable
internal fun MotionTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    Column {
        SectionHeader("Movement")
        LabeledSlider(ParamKeys.SPEED, p.speed, 0.05f..4f) { onChange(p.copy(speed = it)) }
        LabeledSlider(ParamKeys.ZOOM, p.zoom, 0.3f..3f) { onChange(p.copy(zoom = it)) }
        LabeledSlider(ParamKeys.ROTATION, p.rotation, -3f..3f) { onChange(p.copy(rotation = it)) }
        LabeledSlider(ParamKeys.SWAY, p.sway, 0f..1f) { onChange(p.copy(sway = it)) }
        LabeledSlider(ParamKeys.TURBULENCE, p.turbulence, 0f..1.5f) { onChange(p.copy(turbulence = it)) }
        ControlHint("A force inside the scene, not a screen effect.", ParamScope.TURBULENCE)
        SectionHeader("Drift")
        LabeledSlider(ParamKeys.DRIFT_X, p.driftX, -1f..1f) { onChange(p.copy(driftX = it)) }
        LabeledSlider(ParamKeys.DRIFT_Y, p.driftY, -1f..1f) { onChange(p.copy(driftY = it)) }
        SectionHeader("Transient motion")
        ControlHint("How far the picture swells and jumps on each hit the music actually plays.")
        LabeledSlider(ParamKeys.BEAT_PULSE, p.pulse, 0f..1f) { onChange(p.copy(pulse = it)) }
        LabeledSlider(ParamKeys.BEAT_SHAKE, p.shake, 0f..1f) { onChange(p.copy(shake = it)) }
        SectionHeader(ParamKeys.ENDLESS_ZOOM, ParamScope.ENDLESS_ZOOM)
        ControlHint("A dive that never arrives. Dive speed sets the rate.", ParamScope.ENDLESS_ZOOM)
        CheckRow(ParamKeys.ENDLESS_ZOOM, p.endlessZoom) { onChange(p.copy(endlessZoom = it)) }
        if (p.endlessZoom) {
            LabeledSlider(ParamKeys.DIVE_SPEED, p.endlessZoomSpeed, 0.05f..1.2f) { onChange(p.copy(endlessZoomSpeed = it)) }
        }
    }
}

@Composable
internal fun ShapeTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    Column {
        SectionHeader("Beam", ParamScope.BEAM)
        ControlHint(
            "The trace is drawn as a real beam: brightness comes from how long the beam " +
                "dwells, so it glows where the signal turns and dims through fast sweeps.",
            ParamScope.BEAM,
        )
        CheckRow(ParamKeys.XY_PLOT, p.beamXy) { onChange(p.copy(beamXy = it)) }
        LabeledSlider(ParamKeys.BEAM_WIDTH, p.beamWidth, 0.2f..4f) { onChange(p.copy(beamWidth = it)) }
        LabeledSlider(ParamKeys.BEAM_BRIGHTNESS, p.beamIntensity, 0f..3f) { onChange(p.copy(beamIntensity = it)) }
        LabeledSlider(ParamKeys.BEAM_TAIL, p.beamTail, 0f..1f) { onChange(p.copy(beamTail = it)) }
        SectionHeader("Distortion")
        LabeledSlider(ParamKeys.DOMAIN_WARP, p.warp, 0f..1f) { onChange(p.copy(warp = it)) }
        LabeledSlider(ParamKeys.RIPPLE, p.ripple, 0f..1f) { onChange(p.copy(ripple = it)) }
        LabeledSlider(ParamKeys.MORPH, p.morph, 0f..1f) { onChange(p.copy(morph = it)) }
        SectionHeader(ParamKeys.MARCH_DETAIL, ParamScope.MARCH_DETAIL)
        LabeledSlider(ParamKeys.MARCH_DETAIL, p.marchDetail, MarchBudget.MIN_DETAIL..MarchBudget.MAX_DETAIL) {
            onChange(p.copy(marchDetail = it))
        }
        LabeledSlider(ParamKeys.TWIST, p.twist, -1f..1f) { onChange(p.copy(twist = it)) }
        SectionHeader("Symmetry & tiling")
        CheckRow(ParamKeys.KALEIDOSCOPE, p.kaleidoscope) { on ->
            val folds = if (on && p.symmetry < 2) SceneParams.DEFAULT_SYMMETRY_FOLDS else p.symmetry
            onChange(p.copy(kaleidoscope = on, symmetry = folds))
        }
        if (p.kaleidoscope && visible(ParamKeys.KALEIDOSCOPE, ParamScope.of(ParamKeys.KALEIDOSCOPE))) {
            Text("Folds", style = MaterialTheme.typography.labelSmall)
            val folds = SceneParams.SYMMETRY_FOLDS.filter { it >= 2 }
            ChipRow(folds.map { "$it" }, selectedIndex = folds.indexOf(p.symmetry)) { idx ->
                onChange(p.copy(symmetry = folds[idx]))
            }
        }
        CheckRow(ParamKeys.MIRROR, p.mirror) { onChange(p.copy(mirror = it)) }
        LabeledSlider(ParamKeys.TILE, p.tile, 1f..6f) { onChange(p.copy(tile = it)) }
        LabeledSlider(ParamKeys.PIXELATE, p.pixelate, 0f..1f) { onChange(p.copy(pixelate = it)) }
        SectionHeader("Particles", ParamScope.PARTICLE_SPRITE)
        ParamChips(ParamKeys.PARTICLE_SHAPE, SceneParams.PARTICLE_SHAPES, p.particleShape) {
            onChange(p.copy(particleShape = it))
        }
        LabeledSlider(ParamKeys.PARTICLE_SIZE, p.particleSize, 0.3f..2.5f) { onChange(p.copy(particleSize = it)) }
        LabeledSlider(ParamKeys.DENSITY, p.density, 0.1f..1f) { onChange(p.copy(density = it)) }
    }
}

@Composable
internal fun SceneTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    intelligenceMode: IntelligenceMode,
    onIntelligenceModeChange: (IntelligenceMode) -> Unit,
    transitionId: String?,
    transitionDurationSec: Float,
    onTransitionId: (String) -> Unit,
    onTransitionDuration: (Float) -> Unit,
) {
    Column {
        if (transitionId != null && sectionVisible()) {
            SectionHeader("Scene transition")
            val ctx = androidx.compose.ui.platform.LocalContext.current
            val library = remember { dev.geode.render.TransitionCatalog.library(ctx) }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                dev.geode.render.TransitionCatalog.BUILT_IN_IDS.forEach { id ->
                    FilterChip(
                        selected = transitionId == id,
                        onClick = { onTransitionId(id) },
                        label = { Text(id) },
                    )
                }
            }
            if (library.isNotEmpty()) {
                ControlHint(
                    "${library.size} more from the gl-transitions library. Each one blends the " +
                        "outgoing and incoming scenes with the full FX chain already applied, " +
                        "so nothing pops off for the length of a switch.",
                )
                var query by remember { mutableStateOf("") }
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    label = { Text("Search transitions", style = MaterialTheme.typography.labelSmall) },
                )
                val shown =
                    remember(query, library) {
                        if (query.isBlank()) library else library.filter { it.name.contains(query, ignoreCase = true) }
                    }
                androidx.compose.foundation.layout.FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    shown.take(TRANSITION_CHIP_LIMIT).forEach { def ->
                        FilterChip(
                            selected = transitionId == def.name,
                            onClick = { onTransitionId(def.name) },
                            label = { Text(def.name, style = MaterialTheme.typography.labelSmall) },
                        )
                    }
                }
                if (shown.size > TRANSITION_CHIP_LIMIT) {
                    ControlHint("${shown.size - TRANSITION_CHIP_LIMIT} more - narrow the search to reach them.")
                }
            }
            Text("Duration ${"%.1f".format(transitionDurationSec)}s", style = MaterialTheme.typography.labelMedium)
            CrystalSlider(value = transitionDurationSec, onValueChange = onTransitionDuration, valueRange = 0.3f..5f)
        }
        SectionHeader("MilkDrop", ParamScope.MILKDROP)
        CheckRow(ParamKeys.BLEND_PRESET_CHANGES, p.milkdropBlendPresets) {
            onChange(p.copy(milkdropBlendPresets = it))
        }
        if (sectionVisible()) {
            SectionHeader("Scene intelligence")
            ChipRow(IntelligenceMode.entries.map { it.name.lowercase() }, IntelligenceMode.entries.indexOf(intelligenceMode)) {
                onIntelligenceModeChange(IntelligenceMode.entries[it])
            }
        }
    }
}

@Composable
internal fun ReactivityTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    attack: Float,
    decay: Float,
    onReactivityChange: (Float, Float) -> Unit,
    lfos: List<LfoConfig>,
    onLfoChange: (Int, LfoConfig) -> Unit,
    adsr: List<AdsrConfig>,
    onAdsrChange: (Int, AdsrConfig) -> Unit,
) {
    Column {
        SectionHeader("Audio response")
        ControlHint(
            "Everything below reads the signal as it arrives — transients, level, the band " +
                "envelopes, spectral brightness and the stereo field. Nothing here waits for a " +
                "tempo to be worked out, so live input behaves exactly like a file.",
        )
        LabeledSlider(ParamKeys.AUDIO_DRIVE, p.audioDrive, 0.2f..2.5f) { onChange(p.copy(audioDrive = it)) }
        LabeledSlider(ParamKeys.BEAT_RESPONSE, p.beatResponse, 0f..2f) { onChange(p.copy(beatResponse = it)) }
        LabeledSlider(ParamKeys.BEAT_FLASH, p.flash, 0f..1f) { onChange(p.copy(flash = it)) }
        SectionHeader("Band balance", ParamScope.BAND_GAINS)
        LabeledSlider(ParamKeys.BASS_GAIN, p.bassGain, 0f..2f) { onChange(p.copy(bassGain = it)) }
        LabeledSlider(ParamKeys.MID_GAIN, p.midGain, 0f..2f) { onChange(p.copy(midGain = it)) }
        LabeledSlider(ParamKeys.TREBLE_GAIN, p.trebGain, 0f..2f) { onChange(p.copy(trebGain = it)) }
        SectionHeader("Envelope shape", ParamScope.BAND_GAINS)
        ControlHint(
            "How fast every band envelope rises and falls. Short attack snaps, long decay smears.",
            ParamScope.BAND_GAINS,
        )
        LabeledSlider(REACTIVITY_ATTACK, attack, 0.05f..1f, scope = ParamScope.BAND_GAINS) {
            onReactivityChange(it, decay)
        }
        LabeledSlider(REACTIVITY_DECAY, decay, 0.02f..0.6f, scope = ParamScope.BAND_GAINS) {
            onReactivityChange(attack, it)
        }
        SectionHeader("Triggered envelopes")
        ControlHint(
            "Two envelopes fired by the transients that are heard. Each can drive SEVERAL " +
                "parameters (or a modulation slot's rate/depth) at once — tap + to add targets, " +
                "tap a target chip to remove it.",
        )
        if (sectionVisible()) {
            for (i in 0 until AdsrEngine.COUNT) {
                AdsrCard(
                    index = i,
                    config = adsr.getOrElse(i) { AdsrConfig() },
                    onChange = { onAdsrChange(i, it) },
                )
            }
        }
        SectionHeader("Modulation slots")
        ControlHint(
            "Three slots, each pointing one parameter at one source: a free-running LFO, or a " +
                "live follower on a band, the level, the transients, spectral brightness or the " +
                "stereo field. Depth, polarity and curve shape the response. LFO rates are a " +
                "PERIOD IN SECONDS and never sync to a tempo.",
        )
        if (sectionVisible()) {
            for (i in 0 until LfoEngine.SLOTS) {
                ModulatorCard(index = i, config = lfos.getOrElse(i) { LfoConfig() }, onChange = { onLfoChange(i, it) })
            }
        }
    }
}

@Composable
internal fun ColorTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    onTakeArtworkPalette: (() -> Unit)? = null,
    artworkNote: String? = null,
) {
    val palettes = rememberSavedPalettes()
    Column {
        SectionHeader("Palettes")
        if (onTakeArtworkPalette != null && sectionVisible()) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                CrystalButton(compact = true, filled = false, onClick = onTakeArtworkPalette) {
                    Text("Take the colours from the artwork")
                }
            }
            artworkNote?.let { ControlHint(it) }
        }
        if (visible(ParamKeys.PALETTE, ParamScope.UNIVERSAL)) {
            LockableChipLabel(ParamKeys.PALETTE)
            PaletteSlotSelector(p, onChange, palettes)
        }
        if (visible(ParamKeys.COLOUR_MAP, ParamScope.SHADER_LOOK)) {
            LockableChipLabel(ParamKeys.COLOUR_MAP)
            ControlHint(
                "Perceptually even, and cyclic - the two ends join, so a wrap has no seam.",
                ParamScope.SHADER_LOOK,
            )
            Row(
                modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                FilterChip(
                    selected = p.paletteLut < 0,
                    onClick = { onChange(p.copy(paletteLut = SceneParams.NO_PALETTE_LUT)) },
                    label = { Text("off", style = MaterialTheme.typography.labelSmall) },
                )
                SceneParams.CYCLIC_PALETTES.forEachIndexed { index, name ->
                    FilterChip(
                        selected = p.paletteLut == index,
                        onClick = { onChange(p.copy(paletteLut = index)) },
                        label = { Text(name, style = MaterialTheme.typography.labelSmall) },
                    )
                }
            }
        }
        LabeledSlider(ParamKeys.PALETTE_BLEND, p.paletteMix, 0f..1f) { onChange(p.copy(paletteMix = it)) }
        if (p.paletteMix > 0.001f && visible(ParamKeys.PALETTE_2, ParamScope.SHADER_LOOK)) {
            LockableChipLabel(ParamKeys.PALETTE_2)
            PaletteSlotSelector(p, onChange, palettes, second = true)
        }
        ControlHint(
            "MilkDrop presets paint their own colours. This steers them toward the palette " +
                "above; 0 leaves the preset untouched.",
            ParamScope.MILKDROP,
        )
        LabeledSlider(ParamKeys.MILKDROP_PALETTE_TINT, p.milkdropPaletteTint, 0f..1f) {
            onChange(p.copy(milkdropPaletteTint = it))
        }
        if (sectionVisible()) {
            SectionHeader("Gradient & palette maker")
            PaletteMakerCard(p, onChange, palettes)
        }
        SectionHeader("Hue")
        LabeledSlider(ParamKeys.HUE_SHIFT, p.colorShift, 0f..1f) { onChange(p.copy(colorShift = it)) }
        LabeledSlider(ParamKeys.HUE_RANGE, p.hueRange, 0f..1.5f) { onChange(p.copy(hueRange = it)) }
        CheckRow(ParamKeys.COLOR_CYCLE, p.colorCycle) { onChange(p.copy(colorCycle = it)) }
        if (p.colorCycle) {
            LabeledSlider(ParamKeys.CYCLE_SPEED, p.cycleSpeed, 0.02f..0.6f) { onChange(p.copy(cycleSpeed = it)) }
        }
        SectionHeader("Grading")
        LabeledSlider(ParamKeys.SATURATION, p.saturation, 0f..1.5f) { onChange(p.copy(saturation = it)) }
        LabeledSlider(ParamKeys.BRIGHTNESS, p.brightness, 0.2f..2f) { onChange(p.copy(brightness = it)) }
        LabeledSlider(ParamKeys.CONTRAST, p.contrast, 0.3f..2.5f) { onChange(p.copy(contrast = it)) }
        LabeledSlider(ParamKeys.GAMMA, p.gamma, 0.3f..2.5f) { onChange(p.copy(gamma = it)) }
        LabeledSlider(ParamKeys.INTENSITY, p.intensity, 0.2f..2f) { onChange(p.copy(intensity = it)) }
        LabeledSlider(ParamKeys.TEMPERATURE, p.temperature, -1f..1f) { onChange(p.copy(temperature = it)) }
        SectionHeader("Tone effects")
        LabeledSlider(ParamKeys.BLOOM, p.bloom, 0f..1f) { onChange(p.copy(bloom = it)) }
        LabeledSlider(ParamKeys.POSTERIZE, p.posterize, 0f..1f) { onChange(p.copy(posterize = it)) }
        CheckRow(ParamKeys.DUOTONE, p.duotone) { onChange(p.copy(duotone = it)) }
        CheckRow(ParamKeys.SOLARIZE, p.solarize) { onChange(p.copy(solarize = it)) }
        CheckRow(ParamKeys.INVERT, p.invert) { onChange(p.copy(invert = it)) }
    }
}

@Composable
internal fun FxTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    Column {
        SectionHeader("Settings fade (automation)")
        ControlHint("Changes to sliders and preset loads glide to their new values over this time.")
        LabeledSlider(PARAM_FADE, p.paramFadeSec, 0f..5f) { onChange(p.copy(paramFadeSec = it)) }
        SectionHeader("Feedback & trails", ParamScope.TRAIL_LENGTH)
        CheckRow(ParamKeys.TRAILS, p.trails) { onChange(p.copy(trails = it)) }
        LabeledSlider(ParamKeys.TRAIL_LENGTH, p.trailLength, 0.05f..0.98f) { onChange(p.copy(trailLength = it)) }
        LabeledSlider(ParamKeys.TRAIL_ZOOM_ECHO_IN_OUT, p.trailZoom, -0.5f..0.5f) { onChange(p.copy(trailZoom = it)) }
        LabeledSlider(ParamKeys.TRAIL_WARP_LIQUID_ECHO, p.trailWarp, 0f..1f) { onChange(p.copy(trailWarp = it)) }
        SectionHeader("Screen FX")
        LabeledSlider(ParamKeys.CHROMATIC_ABERRATION, p.chromaAb, 0f..1f) { onChange(p.copy(chromaAb = it)) }
        LabeledSlider(ParamKeys.VIGNETTE, p.vignette, 0f..1f) { onChange(p.copy(vignette = it)) }
        LabeledSlider(ParamKeys.SCANLINES, p.scanlines, 0f..1f) { onChange(p.copy(scanlines = it)) }
        LabeledSlider(ParamKeys.FILM_GRAIN, p.grain, 0f..1f) { onChange(p.copy(grain = it)) }
        LabeledSlider(ParamKeys.GLITCH, p.glitch, 0f..1f) { onChange(p.copy(glitch = it)) }
        LabeledSlider(ParamKeys.FISHEYE, p.fisheye, -1f..1f) { onChange(p.copy(fisheye = it)) }
        LabeledSlider(ParamKeys.STROBE, p.strobe, 0f..1f) { onChange(p.copy(strobe = it)) }
        if (sectionVisible()) LayersSection()
    }
}

@Composable
private fun LayersSection() {
    val layers by LayersBus.state.collectAsStateWithLifecycle()
    val layerScenes by LayersBus.availableScenes.collectAsStateWithLifecycle()
    val activeScene by LayersBus.activeSceneId.collectAsStateWithLifecycle()
    SectionHeader("Layers (second style)")
    ControlHint(
        "Renders a second style every frame and blends it under the active " +
            "one - a whole extra scene, so it costs frames. Screen only: a " +
            "video export carries the active style alone.",
    )
    CheckRow(LAYERS_ENABLED, layers.enabled) { on ->
        val scene = layers.sceneId ?: layerScenes.firstOrNull { it != activeScene }
        LayersBus.state.value = layers.copy(enabled = on, sceneId = scene)
    }
    if (!layers.enabled) return
    var showLayerPicker by remember { mutableStateOf(false) }
    Box {
        TextButton(onClick = { showLayerPicker = true }) {
            Text("Layer style: ${layers.sceneId?.let { sceneDisplayLabel(it) } ?: "none"}")
        }
        DropdownMenu(expanded = showLayerPicker, onDismissRequest = { showLayerPicker = false }) {
            layerScenes.filter { it != activeScene }.forEach { id ->
                DropdownMenuItem(
                    text = { Text(sceneDisplayLabel(id)) },
                    onClick = {
                        LayersBus.state.value = layers.copy(sceneId = id)
                        showLayerPicker = false
                    },
                )
            }
        }
    }
    if (layers.sceneId != null && layers.sceneId == activeScene) {
        ControlHint("That style is now the active one, so the layer is idle - pick another.")
    }
    Text("Blend", style = MaterialTheme.typography.labelSmall)
    ChipRow(BlendMode.entries.map { it.name.lowercase() }, BlendMode.entries.indexOf(layers.blend)) {
        LayersBus.state.value = layers.copy(blend = BlendMode.entries[it])
    }
    Text("Layer mix ${"%.2f".format(layers.mix)}", style = MaterialTheme.typography.labelMedium)
    CrystalSlider(
        value = layers.mix,
        onValueChange = { LayersBus.state.value = layers.copy(mix = it) },
        valueRange = 0f..1f,
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
private fun ModulatorCard(
    index: Int,
    config: LfoConfig,
    onChange: (LfoConfig) -> Unit,
) {
    var showTargetPicker by remember { mutableStateOf(false) }
    val sceneId = LocalSceneId.current
    Column(modifier = Modifier.padding(vertical = 6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Slot ${index + 1}", style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
            Switch(checked = config.enabled, onCheckedChange = { onChange(config.copy(enabled = it)) })
        }
        if (config.enabled) {
            TextButton(onClick = { showTargetPicker = true }) {
                Text("Target: ${config.target.label}")
            }
            if (config.target != LfoTarget.NONE && !config.target.scope.appliesTo(sceneId)) {
                ControlHint("${config.target.label} does nothing on this style - pick another target.")
            }
            Text("Source", style = MaterialTheme.typography.labelSmall)
            ChipRow(ModSource.entries.map { it.label }, ModSource.entries.indexOf(config.source)) {
                val source = ModSource.entries[it]
                onChange(config.copy(source = source, polarity = LfoConfig.naturalPolarity(source)))
            }
            if (config.source == ModSource.LFO) {
                ChipRow(LfoWave.entries.map { it.label }, LfoWave.entries.indexOf(config.wave)) {
                    onChange(config.copy(wave = LfoWave.entries[it]))
                }
                RateSecondsSlider(config.rateSeconds) { onChange(config.copy(rateSeconds = it)) }
            }
            LabeledSlider(MOD_DEPTH, config.depth, 0f..1f) { onChange(config.copy(depth = it)) }
            Text("Polarity", style = MaterialTheme.typography.labelSmall)
            ChipRow(ModPolarity.entries.map { it.label }, ModPolarity.entries.indexOf(config.polarity)) {
                onChange(config.copy(polarity = ModPolarity.entries[it]))
            }
            Text("Curve", style = MaterialTheme.typography.labelSmall)
            ChipRow(ModCurve.entries.map { it.label }, ModCurve.entries.indexOf(config.curve)) {
                onChange(config.copy(curve = ModCurve.entries[it]))
            }
        }
    }
    if (showTargetPicker) {
        AlertDialog(
            onDismissRequest = { showTargetPicker = false },
            title = { Text("Slot ${index + 1} target") },
            text = {
                Column(modifier = Modifier.height(360.dp).verticalScroll(rememberScrollState())) {
                    LfoTarget.entries.forEach { t ->
                        // A chain target may only point at a LATER slot, and a scene-parameter
                        // target is only offered where it can actually move the picture.
                        val chain = t.chain
                        val offered = if (chain != null) index < chain.slot else t.scope.appliesTo(sceneId)
                        if (offered) {
                            TextButton(onClick = {
                                onChange(config.copy(target = t))
                                showTargetPicker = false
                            }) {
                                Text(if (t == config.target) "▶ ${t.label}" else t.label)
                            }
                        }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { showTargetPicker = false }) { Text("Close") } },
        )
    }
}

/**
 * The LFO rate, as a period in seconds.
 *
 * Slid on a log scale because the useful span runs from a twentieth of a second to a minute, and
 * a linear track would spend nine tenths of its length between 6 s and 60 s.
 */
@Composable
private fun RateSecondsSlider(
    seconds: Float,
    onChange: (Float) -> Unit,
) {
    val lo = LfoConfig.MIN_RATE_SECONDS
    val hi = LfoConfig.MAX_RATE_SECONDS
    val t = (ln(seconds.coerceIn(lo, hi) / lo) / ln(hi / lo)).coerceIn(0f, 1f)
    Column(Modifier.padding(vertical = 2.dp)) {
        Text("Rate ${"%.2f".format(seconds)} s per cycle", style = MaterialTheme.typography.labelSmall)
        CrystalSlider(
            value = t,
            onValueChange = { onChange(lo * (hi / lo).pow(it)) },
            valueRange = 0f..1f,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

@Composable
private fun ChipRow(
    labels: List<String>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        labels.forEachIndexed { index, label ->
            FilterChip(
                selected = index == selectedIndex,
                onClick = { onSelect(index) },
                label = { Text(label, style = MaterialTheme.typography.labelSmall) },
            )
        }
    }
}

/** A named, lockable chip group. Hidden as one unit when the parameter is not live here. */
@Composable
private fun ParamChips(
    label: String,
    options: List<String>,
    selectedIndex: Int,
    scope: ParamScope = ParamScope.of(label),
    onSelect: (Int) -> Unit,
) {
    if (!visible(label, scope)) return
    ControlLabelRow(label, label)
    ChipRow(options, selectedIndex, onSelect = onSelect)
}

@Composable
private fun LabeledSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    display: String = label,
    scope: ParamScope = ParamScope.of(label),
    onChange: (Float) -> Unit,
) {
    if (!visible(label, scope)) return
    Column(Modifier.padding(vertical = 2.dp)) {
        ControlLabelRow("$display ${"%.2f".format(value)}", label)
        CrystalSlider(value = value, onValueChange = onChange, valueRange = range, modifier = Modifier.fillMaxWidth())
    }
}

@Composable
private fun LockableChipLabel(
    label: String,
    display: String = label,
) = ControlLabelRow(display, label)

@Composable
private fun CheckRow(
    label: String,
    checked: Boolean,
    display: String = label,
    scope: ParamScope = ParamScope.of(label),
    onChange: (Boolean) -> Unit,
) {
    if (!visible(label, scope)) return
    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked = checked, onCheckedChange = onChange)
        Text(display, style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(1f))
        LockChip(label)
    }
}

@Composable
internal fun FluidTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    injectionError: String? = null,
    onApplyInjectionShaders: (String?, String?) -> Unit = { _, _ -> },
) {
    val sceneId = LocalSceneId.current
    val isFluidScene = ParamScope.FLUID_SIM.appliesTo(sceneId)
    val isWaterScene = ParamScope.WATER.appliesTo(sceneId)
    Column {
        SectionHeader("Water", ParamScope.WATER)
        ControlHint(
            "Heightfield water: hits drop expanding rings, stirrers carve wakes, the journey " +
                "decides where they land.",
            ParamScope.WATER,
        )
        LabeledSlider(ParamKeys.RIPPLE_STRENGTH, p.waterRippleStrength, 0f..2f) { onChange(p.copy(waterRippleStrength = it)) }
        LabeledSlider(ParamKeys.DEPTH, p.waterDepth, 0f..1f) { onChange(p.copy(waterDepth = it)) }
        LabeledSlider(ParamKeys.SPECULAR, p.waterSpecular, 0f..1f) { onChange(p.copy(waterSpecular = it)) }
        LabeledSlider(ParamKeys.FLOW_DRIFT, p.waterFlow, 0f..1f) { onChange(p.copy(waterFlow = it)) }
        if (isWaterScene) {
            LabeledSlider(ParamKeys.WAVE_SPEED, p.waterWaveSpeed, 0.2f..2f) { onChange(p.copy(waterWaveSpeed = it)) }
            LabeledSlider(ParamKeys.DAMPING, p.waterDamping, 0.9f..0.999f) { onChange(p.copy(waterDamping = it)) }
        }
        SectionHeader(ParamKeys.LIQUID, ParamScope.WATER)
        ControlHint(
            "Every splash stains a colour film that the surface then carries. Turn it down for " +
                "a plain depth-graded pool.",
            ParamScope.WATER,
        )
        LabeledSlider(ParamKeys.LIQUID, p.waterLiquid, 0f..1f) { onChange(p.copy(waterLiquid = it)) }
        if (p.waterLiquid > 0.001f) {
            LabeledSlider(ParamKeys.LIQUID_FLOW, p.waterLiquidFlow, 0f..4f) { onChange(p.copy(waterLiquidFlow = it)) }
            LabeledSlider(ParamKeys.LIQUID_FADE, p.waterLiquidFade, 0f..2f) { onChange(p.copy(waterLiquidFade = it)) }
        }
        SectionHeader("Journey (spawn & catch progression)", ParamScope.JOURNEY)
        ControlHint(
            "Spawn points birth particles and dye; catch points pull them in and recycle them. " +
                "Both travel: heard energy walks the layout on, and a change of spectral " +
                "character re-seats it.",
            ParamScope.JOURNEY,
        )
        ParamChips(ParamKeys.PATH, SceneParams.FLUID_PATHS, p.fluidSpawnPath.coerceIn(0, SceneParams.FLUID_PATHS.size - 1)) {
            onChange(p.copy(fluidSpawnPath = it))
        }
        LabeledIntSlider(ParamKeys.SPAWN_POINTS, p.fluidSpawnPoints, 1..8) { onChange(p.copy(fluidSpawnPoints = it)) }
        LabeledSlider(PROGRESSION, p.fluidSpawnProgress, 0f..1f, scope = ParamScope.JOURNEY) {
            onChange(p.copy(fluidSpawnProgress = it))
        }
        LabeledIntSlider(ParamKeys.CATCH_POINTS, p.fluidCatchPoints, 0..4) { onChange(p.copy(fluidCatchPoints = it)) }
        if (p.fluidCatchPoints > 0) {
            LabeledSlider(ParamKeys.CATCH_PULL, p.fluidCatchPull, 0f..3f) { onChange(p.copy(fluidCatchPull = it)) }
            LabeledSlider(ParamKeys.CATCH_RADIUS, p.fluidCatchRadius, 0.03f..0.3f) { onChange(p.copy(fluidCatchRadius = it)) }
        }
        if (sectionVisible(ParamScope.EMITTERS)) {
            SectionHeader("Quality", ParamScope.EMITTERS)
            ChipRow(
                dev.geode.render.fluid.FluidQuality.LABELS,
                p.fluidQuality.coerceIn(0, dev.geode.render.fluid.FluidQuality.LABELS.size - 1),
            ) { onChange(p.copy(fluidQuality = it)) }
            CheckRow(AUTO_QUALITY, p.fluidAutoQuality, scope = ParamScope.EMITTERS) { onChange(p.copy(fluidAutoQuality = it)) }
        }
        LabeledIntSlider(ParamKeys.SOLVER_ITERATIONS, p.fluidIterations, 8..40) { onChange(p.copy(fluidIterations = it)) }
        SectionHeader("Character", ParamScope.FLUID_SIM)
        LabeledSlider(ParamKeys.FLUID_CURL, p.fluidCurl, 0f..50f) { onChange(p.copy(fluidCurl = it)) }
        LabeledSlider(ParamKeys.MOTION_FADE, p.fluidVelocityDissipation, 0f..4f) { onChange(p.copy(fluidVelocityDissipation = it)) }
        LabeledSlider(ParamKeys.FLUID_FADE, p.fluidDensityDissipation, 0f..4f) { onChange(p.copy(fluidDensityDissipation = it)) }
        LabeledSlider(ParamKeys.CHROMATIC_AGING, p.fluidChromaticAging, 0f..1f) { onChange(p.copy(fluidChromaticAging = it)) }
        LabeledSlider(ParamKeys.PRESSURE, p.fluidPressure, 0f..1f) { onChange(p.copy(fluidPressure = it)) }
        SectionHeader("Emitters", ParamScope.EMITTERS)
        ParamChips(ParamKeys.BEAT_PATTERN, SceneParams.FLUID_PATTERNS, p.fluidBeatPattern.coerceIn(0, 3)) {
            onChange(p.copy(fluidBeatPattern = it))
        }
        LabeledIntSlider(ParamKeys.BEAT_SPLATS, p.fluidBeatSplats, 0..8) { onChange(p.copy(fluidBeatSplats = it)) }
        LabeledIntSlider(ParamKeys.STIRRERS, p.fluidStirrers, 0..4) { onChange(p.copy(fluidStirrers = it)) }
        LabeledSlider(ParamKeys.STIRRER_SPEED, p.fluidStirrerSpeed, 0f..2f) { onChange(p.copy(fluidStirrerSpeed = it)) }
        LabeledSlider(ParamKeys.FLUID_SPLAT_RADIUS, p.fluidSplatRadius, 0.02f..0.4f) { onChange(p.copy(fluidSplatRadius = it)) }
        LabeledSlider(ParamKeys.RADIUS_ON_BEAT, p.fluidRadiusPulse, 0f..1f) { onChange(p.copy(fluidRadiusPulse = it)) }
        LabeledSlider(ParamKeys.FLUID_SPLAT_FORCE, p.fluidSplatForce, 0f..3f) { onChange(p.copy(fluidSplatForce = it)) }
        CheckRow(ParamKeys.BASS_PUMP, p.fluidBassPump) { onChange(p.copy(fluidBassPump = it)) }
        CheckRow(ParamKeys.TREBLE_SPARKLE, p.fluidSparkle) { onChange(p.copy(fluidSparkle = it)) }
        LabeledSlider(ParamKeys.PALETTE_CYCLE, p.fluidPaletteCycleSpeed, 0f..2f) { onChange(p.copy(fluidPaletteCycleSpeed = it)) }
        SectionHeader("Particles", ParamScope.PARTICLE_SPRITE)
        if (isFluidScene) {
            CheckRow(PARTICLE_LAYER, p.fluidParticlesEnabled, scope = ParamScope.FLUID_SIM) {
                onChange(p.copy(fluidParticlesEnabled = it))
            }
        }
        if (!isFluidScene || p.fluidParticlesEnabled) {
            LabeledSlider(ParamKeys.PARTICLE_DRAG, p.fluidParticleDrag, 0.02f..1f) { onChange(p.copy(fluidParticleDrag = it)) }
            LabeledSlider(ParamKeys.PARTICLE_LIFE_S, p.fluidParticleLife, 1f..20f) { onChange(p.copy(fluidParticleLife = it)) }
            LabeledSlider(ParamKeys.PARTICLE_BRIGHTNESS, p.fluidParticleBrightness, 0f..2f) {
                onChange(p.copy(fluidParticleBrightness = it))
            }
        }
        if (isFluidScene) {
            CheckRow(INK_LAYER, p.fluidDyeEnabled, scope = ParamScope.FLUID_SIM) { onChange(p.copy(fluidDyeEnabled = it)) }
        }
        SectionHeader("Look", ParamScope.FLUID_SIM)
        CheckRow(ParamKeys.SHADING_EMBOSSED_INK, p.fluidShading) { onChange(p.copy(fluidShading = it)) }
        CheckRow(ParamKeys.GLOW_FLUID, p.fluidBloom) { onChange(p.copy(fluidBloom = it)) }
        if (p.fluidBloom) {
            LabeledSlider(ParamKeys.FLUID_GLOW, p.fluidBloomIntensity, 0.1f..2f) { onChange(p.copy(fluidBloomIntensity = it)) }
            LabeledSlider(ParamKeys.GLOW_THRESHOLD, p.fluidBloomThreshold, 0f..1f) { onChange(p.copy(fluidBloomThreshold = it)) }
        }
        CheckRow(ParamKeys.SUNRAYS, p.fluidSunrays) { onChange(p.copy(fluidSunrays = it)) }
        if (p.fluidSunrays) {
            LabeledSlider(ParamKeys.SUNRAYS_WEIGHT, p.fluidSunraysWeight, 0.3f..1f) { onChange(p.copy(fluidSunraysWeight = it)) }
        }
        SectionHeader("Audio routing", ParamScope.FLUID_SIM)
        LabeledSlider(ParamKeys.CURL_FROM_MIDS, p.fluidCurlAudio, 0f..1f) { onChange(p.copy(fluidCurlAudio = it)) }
        LabeledSlider(ParamKeys.GLOW_FROM_LOUDNESS, p.fluidBloomAudio, 0f..1f) { onChange(p.copy(fluidBloomAudio = it)) }
        LabeledSlider(ParamKeys.FADE_WHEN_QUIET, p.fluidFadeAudio, 0f..1f) { onChange(p.copy(fluidFadeAudio = it)) }
        SectionHeader("FlowField (all styles)")
        ControlHint(
            "A shared fluid velocity field that bends ANY style: it distorts the composite " +
                "output (particles, shaders, MilkDrop - and exports), particle scenes can ride " +
                "the field, and shader scenes see it as uFlow.",
        )
        CheckRow(FLOWFIELD_ENABLED, p.flowEnabled) { onChange(p.copy(flowEnabled = it)) }
        if (p.flowEnabled) {
            LabeledSlider(ParamKeys.FLOW_STRENGTH, p.flowStrength, 0f..1f) { onChange(p.copy(flowStrength = it)) }
            LabeledSlider(ParamKeys.FLOW_FORCE, p.flowForce, 0f..3f) { onChange(p.copy(flowForce = it)) }
            LabeledSlider(ParamKeys.FLOW_CURL, p.flowCurl, 0f..50f) { onChange(p.copy(flowCurl = it)) }
        }
        SectionHeader("Water ripples (all styles)", ParamScope.RIPPLE_OVERLAY)
        ControlHint(
            "The water heightfield rides on top of ANY style: hits drop rings that refract the " +
                "image, treble sprinkles small drops, and glint adds a specular sparkle on the " +
                "crests.",
            ParamScope.RIPPLE_OVERLAY,
        )
        CheckRow(RIPPLES_ENABLED, p.rippleOverlayEnabled, scope = ParamScope.RIPPLE_OVERLAY) {
            onChange(p.copy(rippleOverlayEnabled = it))
        }
        if (p.rippleOverlayEnabled && !isWaterScene) {
            LabeledSlider(ParamKeys.WAVE_SPEED, p.waterWaveSpeed, 0.2f..2f) { onChange(p.copy(waterWaveSpeed = it)) }
            LabeledSlider(ParamKeys.DAMPING, p.waterDamping, 0.9f..0.999f) { onChange(p.copy(waterDamping = it)) }
            LabeledSlider(ParamKeys.RIPPLE_OVERLAY_STRENGTH, p.rippleOverlayStrength, 0f..1f) {
                onChange(p.copy(rippleOverlayStrength = it))
            }
            LabeledSlider(ParamKeys.RIPPLE_GLINT, p.rippleOverlaySpecular, 0f..1f) { onChange(p.copy(rippleOverlaySpecular = it)) }
        }
        if (isFluidScene && sectionVisible()) {
            InjectionShaderEditors(injectionError, onApplyInjectionShaders)
        }
    }
}

@Composable
private fun InjectionShaderEditors(
    injectionError: String?,
    onApplyInjectionShaders: (String?, String?) -> Unit,
) {
    SectionHeader("Injection shaders (advanced)")
    ControlHint(
        "The force and dye injection passes are user-replaceable GLSL. " +
            "Both start from the built-in capsule splat (uMode 0 = " +
            "velocity, 1 = dye); a failed compile keeps the last good " +
            "program. Extra uniforms: uDt, uDx, uTime, uBass, uMid, " +
            "uTreble, uEnergy, uBeat. Clear the text to restore built-ins.",
    )
    val ctx = androidx.compose.ui.platform.LocalContext.current
    val template =
        remember {
            dev.geode.render.fluid.FluidShaderTemplate
                .splat(ctx)
        }
    var forceSrc by remember { mutableStateOf(template) }
    var dyeSrc by remember { mutableStateOf(template) }
    var editorsUsed by remember { mutableStateOf(false) }
    Text("Force shader", style = MaterialTheme.typography.labelSmall)
    OutlinedTextField(
        value = forceSrc,
        onValueChange = {
            forceSrc = it
            editorsUsed = true
        },
        modifier = Modifier.fillMaxWidth().height(180.dp),
        textStyle = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
    )
    Text("Dye shader", style = MaterialTheme.typography.labelSmall)
    OutlinedTextField(
        value = dyeSrc,
        onValueChange = {
            dyeSrc = it
            editorsUsed = true
        },
        modifier = Modifier.fillMaxWidth().height(180.dp),
        textStyle = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
    )
    if (injectionError != null) {
        Text(injectionError, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.labelSmall)
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        TextButton(onClick = {
            val f = forceSrc.takeIf { editorsUsed && it.isNotBlank() && it != template }
            val d = dyeSrc.takeIf { editorsUsed && it.isNotBlank() && it != template }
            onApplyInjectionShaders(f, d)
        }) { Text("Apply shaders") }
        TextButton(onClick = {
            forceSrc = template
            dyeSrc = template
            onApplyInjectionShaders(null, null)
        }) { Text("Reset to built-in") }
    }
}

@Composable
internal fun CymaticsTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val activeSceneId = LocalSceneId.current
    Column {
        SectionHeader("Wave", ParamScope.CYMATICS)
        ControlHint(
            "The standing-wave field of whatever is playing - or of the mic, on live input - " +
                "drawn fullscreen. A pure tone gives one clean symmetric figure, a chord the " +
                "superposition of its notes'.",
            ParamScope.CYMATICS,
        )
        val forcedGeometry =
            VisualStyleCatalog
                .cymatics(activeSceneId)
                ?.geometryOverride
                ?.let { SceneParams.CYMATICS_GEOMETRIES.getOrNull(it) }
        if (forcedGeometry != null) {
            ControlHint("Geometry set by this style: $forcedGeometry.", ParamScope.CYMATICS)
        } else {
            ParamChips(ParamKeys.GEOMETRY, SceneParams.CYMATICS_GEOMETRIES, p.cymaticsGeometry) {
                onChange(p.copy(cymaticsGeometry = it))
            }
        }
        LabeledSlider(
            ParamKeys.FUNDAMENTAL_HZ,
            p.cymaticsFundamental,
            CymaticsMath.MIN_FUNDAMENTAL_HZ..CymaticsMath.MAX_FUNDAMENTAL_HZ,
        ) { onChange(p.copy(cymaticsFundamental = it)) }
        LabeledIntSlider(ParamKeys.STANDING_WAVES, p.cymaticsModes, 1..CymaticsMath.MAX_RENDERED_MODES) {
            onChange(p.copy(cymaticsModes = it))
        }
        LabeledSlider(ParamKeys.TONAL_FOCUS, p.cymaticsFocus, 0f..1f) { onChange(p.copy(cymaticsFocus = it)) }
        LabeledSlider(ParamKeys.PLATE_RING, p.cymaticsRing, 0f..1f) { onChange(p.copy(cymaticsRing = it)) }

        SectionHeader("Field", ParamScope.CYMATICS)
        ControlHint("How much of the wave field fills the screen, and how it moves through it.", ParamScope.CYMATICS)
        LabeledSlider(ParamKeys.FIELD_SCALE, p.cymaticsScale, 0.5f..8f) { onChange(p.copy(cymaticsScale = it)) }
        LabeledSlider(ParamKeys.WAVE_FLOW, p.cymaticsFlow, 0f..1f) { onChange(p.copy(cymaticsFlow = it)) }
        LabeledSlider(ParamKeys.FIELD_SWIRL, p.cymaticsSwirl, -1f..1f) { onChange(p.copy(cymaticsSwirl = it)) }

        SectionHeader("Look", ParamScope.CYMATICS)
        ControlHint(
            "Fill runs from bare nodal filigree on dark cells to a fully filled iridescent surface.",
            ParamScope.CYMATICS,
        )
        LabeledSlider(ParamKeys.FILL, p.cymaticsFill, 0f..1f) { onChange(p.copy(cymaticsFill = it)) }
        LabeledSlider(ParamKeys.NODAL_LINES, p.cymaticsLine, 0f..2f) { onChange(p.copy(cymaticsLine = it)) }
        LabeledSlider(ParamKeys.NODAL_GLOW, p.cymaticsGlow, 0f..2f) { onChange(p.copy(cymaticsGlow = it)) }
        LabeledSlider(ParamKeys.IRIDESCENCE, p.cymaticsIridescence, 0f..1f) { onChange(p.copy(cymaticsIridescence = it)) }
        LabeledSlider(ParamKeys.CAUSTIC_SHEEN, p.cymaticsCaustic, 0f..1.5f) { onChange(p.copy(cymaticsCaustic = it)) }
    }
}

@Composable
private fun LabeledIntSlider(
    label: String,
    value: Int,
    range: IntRange,
    display: String = label,
    scope: ParamScope = ParamScope.of(label),
    onChange: (Int) -> Unit,
) {
    if (!visible(label, scope)) return
    Column(Modifier.padding(vertical = 2.dp)) {
        ControlLabelRow("$display $value", label)
        CrystalSlider(
            value = value.toFloat(),
            onValueChange = { onChange(it.toInt().coerceIn(range.first, range.last)) },
            valueRange = range.first.toFloat()..range.last.toFloat(),
            steps = (range.last - range.first - 1).coerceAtLeast(0),
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

@OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)
@Composable
private fun AdsrCard(
    index: Int,
    config: AdsrConfig,
    onChange: (AdsrConfig) -> Unit,
) {
    var showAdd by remember { mutableStateOf(false) }
    val sceneId = LocalSceneId.current
    Column(modifier = Modifier.padding(vertical = 4.dp)) {
        CheckRow("Envelope ${index + 1} on", config.enabled) { onChange(config.copy(enabled = it)) }
        if (!config.enabled) return@Column
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Targets:", style = MaterialTheme.typography.labelSmall)
        }
        androidx.compose.foundation.layout.FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            config.targets.forEach { t ->
                AssistChip(
                    onClick = { onChange(config.copy(targets = config.targets - t)) },
                    label = { Text(t.label, style = MaterialTheme.typography.labelSmall) },
                )
            }
            Box {
                AssistChip(
                    onClick = { showAdd = true },
                    label = { Text("+", style = MaterialTheme.typography.labelSmall) },
                )
                DropdownMenu(expanded = showAdd, onDismissRequest = { showAdd = false }) {
                    LfoTarget.entries
                        .filter {
                            it != LfoTarget.NONE &&
                                it !in config.targets &&
                                (it.chain != null || it.scope.appliesTo(sceneId))
                        }.forEach { t ->
                            DropdownMenuItem(
                                text = { Text(t.label) },
                                onClick = {
                                    onChange(config.copy(targets = config.targets + t))
                                    showAdd = false
                                },
                            )
                        }
                }
            }
        }
        CardSubHeader("Shape")
        LabeledSlider(ENV_ATTACK, config.attack, 0.005f..1f) { onChange(config.copy(attack = it)) }
        LabeledSlider(ENV_DECAY, config.decay, 0.01f..1.5f) { onChange(config.copy(decay = it)) }
        LabeledSlider(ENV_SUSTAIN, config.sustain, 0f..1f) { onChange(config.copy(sustain = it)) }
        LabeledSlider(ENV_RELEASE, config.release, 0.02f..2f) { onChange(config.copy(release = it)) }
        LabeledSlider(ENV_AMOUNT, config.amount, 0f..1.5f) { onChange(config.copy(amount = it)) }
        CardSubHeader("Sustain gate")
        ControlHint(
            "The envelope holds while the chosen source stays above the gate and releases when " +
                "it drops below - so the same hit can open a long swell in a loud chorus and a " +
                "short blip in a quiet verse.",
        )
        LockableChipLabel(ENV_GATE_SOURCE)
        ChipRow(EnvBand.entries.map { it.label }, EnvBand.entries.indexOf(config.band)) {
            onChange(config.copy(band = EnvBand.entries[it]))
        }
        LabeledSlider(ENV_GATE_LEVEL, config.gateThreshold, 0.05f..1f) {
            onChange(config.copy(gateThreshold = it))
        }
        CheckRow(ENV_SUSTAIN_TRACK, config.sustainTrack) { onChange(config.copy(sustainTrack = it)) }
        CheckRow(ENV_RETRIGGER, config.retrigger) { onChange(config.copy(retrigger = it)) }
    }
}

@Composable
private fun CardSubHeader(title: String) {
    if (!sectionVisible()) return
    Text(
        title,
        modifier = Modifier.padding(top = 10.dp, bottom = 2.dp),
        style = MaterialTheme.typography.labelSmall,
        color = accentTextColor(),
    )
}

// Names for the controls that are panel machinery rather than scene parameters. They live in one
// place so the search matches them and nothing has to spell a string twice.
private const val REACTIVITY_ATTACK = "Envelope attack"
private const val REACTIVITY_DECAY = "Envelope decay"
private const val PARAM_FADE = "Fade time (s)"
private const val MOD_DEPTH = "Slot depth"
private const val ENV_ATTACK = "Env attack"
private const val ENV_DECAY = "Env decay"
private const val ENV_SUSTAIN = "Sustain"
private const val ENV_RELEASE = "Release"
private const val ENV_AMOUNT = "Amount"
private const val ENV_GATE_SOURCE = "Gate source"
private const val ENV_GATE_LEVEL = "Gate level"
private const val ENV_SUSTAIN_TRACK = "Sustain follows source"
private const val ENV_RETRIGGER = "Retrigger on every hit"
private const val PROGRESSION = "Progression"
private const val AUTO_QUALITY = "Auto quality (downgrade on sustained low FPS)"
private const val PARTICLE_LAYER = "Particle layer"
private const val INK_LAYER = "Ink layer"
private const val FLOWFIELD_ENABLED = "FlowField enabled"
private const val RIPPLES_ENABLED = "Water ripples enabled"
private const val LAYERS_ENABLED = "Layers enabled"

/** Wraps a tab's content in the scene + search context every control reads. */
@Composable
internal fun ParamSurface(
    sceneId: String,
    query: String,
    content: @Composable () -> Unit,
) = CompositionLocalProvider(
    LocalSceneId provides sceneId,
    LocalParamQuery provides query,
    content = content,
)
