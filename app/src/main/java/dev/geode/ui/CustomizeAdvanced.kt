@file:Suppress("MatchingDeclarationName")

package dev.geode.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.RingLog
import dev.geode.data.NoteResponseStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.AdsrConfig
import dev.geode.render.BlendMode
import dev.geode.render.EnvBand
import dev.geode.render.EventRecipe
import dev.geode.render.LfoConfig
import dev.geode.render.LfoTarget
import dev.geode.render.LfoWave
import dev.geode.render.ModCurve
import dev.geode.render.ModPolarity
import dev.geode.render.ModSource
import dev.geode.render.ModTable
import dev.geode.render.ReactMode
import dev.geode.render.ReactSource
import dev.geode.render.ReactSourceGroup
import dev.geode.render.ReactionRoute
import dev.geode.render.TransitionCatalog
import dev.geode.render.VisualizerView
import dev.geode.render.fluid.FluidShaderTemplate
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.ParamFormat
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.ParamUnit
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneParams
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.UiSound
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.ln
import kotlin.math.pow
import kotlin.reflect.KProperty1

/** What a deeper page edits, gathered once so each page reads plain values. */
internal class AdvancedHost(
    val viewModel: PlayerViewModel,
    val visualsViewModel: VisualsViewModel,
    val visualizerView: VisualizerView,
)

/**
 * The ♛ that marks Pro on a page or a row: Customize's pages and cards and the Styles list draw this
 * one, so Pro reads the same everywhere. A real crown ([StoneIcon.CROWN]); the Material Icons
 * `WorkspacePremium` it replaced is a rosette, which device QA read as a ribbon badge.
 */
@Composable
internal fun ProCrown(modifier: Modifier = Modifier) {
    StoneIconArt(
        StoneIcon.CROWN,
        contentDescription = stringResource(R.string.customize_a11y_pro),
        modifier = modifier.padding(horizontal = 4.dp).size(18.dp),
        tint = accentTextColor(),
    )
}

/**
 * Music › Reactions: the route list.
 *
 * Shows the style's own reactions until the user edits one; the first edit copies them into the
 * user's own list, and "Use the style's own reactions" drops it again. The moments and their recipes
 * are Music › Moments, and the reaction strength is Music's Basics.
 */
@Composable
internal fun ReactionsSection(
    p: SceneParams,
    reactions: List<ReactionRoute>?,
    onReactionsChange: (List<ReactionRoute>?) -> Unit,
    onReroll: () -> Unit,
    handMask: Int = GeodeNative.HAND_ALL,
) {
    if (!sectionVisible()) return
    val sceneId = LocalSceneId.current
    ControlHint(
        if (reactions == null) {
            stringResource(R.string.customize_reactions_hint_style)
        } else {
            stringResource(R.string.customize_reactions_hint_own)
        },
    )
    // What plays: with moments set on Customize › Music, the user's routes there and the live DNA elsewhere
    // (native's merge), so an edit here starts from the matrix as it sounds and makes the whole list the user's.
    val routes =
        remember(sceneId, p.reactSeed, reactions, handMask) {
            BuildStyleReactionsUseCase().playing(sceneId, p.reactSeed, reactions, handMask)
        }
    val sounds = LocalUiSounds.current
    val tap: (() -> Unit) -> Unit = { action ->
        sounds.play(UiSound.TAP)
        action()
    }
    FlowRow(horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
        FilledTonalButton(onClick = { tap(onReroll) }) { Text(stringResource(R.string.customize_reactions_shuffle)) }
        if (reactions != null) {
            FilledTonalButton(onClick = { tap { onReactionsChange(null) } }) {
                Text(stringResource(R.string.customize_reactions_use_style))
            }
        }
        if (routes.size < ReactionRoute.MAX_ROUTES) {
            FilledTonalButton(onClick = { tap { onReactionsChange(routes + NEW_REACTION) } }) { Text(stringResource(R.string.action_add)) }
        }
    }
    routes.forEachIndexed { i, route ->
        ReactionCard(
            route = route,
            onChange = { changed -> onReactionsChange(routes.toMutableList().also { it[i] = changed }) },
            onRemove = { onReactionsChange(routes.filterIndexed { j, _ -> j != i }) },
        )
    }
}

private val NEW_REACTION =
    ReactionRoute(source = ReactSource.ONSET, target = LfoTarget.COLOR_SHIFT, mode = ReactMode.STEP, amount = 0.15f)

// The whole range native accepts: the style's own reactions draw drop surges above 1.
private val REACT_AMOUNT_RANGE = ReactionRoute.MIN_AMOUNT..ReactionRoute.MAX_AMOUNT

private enum class ReactionPick { SOURCE, TARGET }

@Composable
private fun ReactionCard(
    route: ReactionRoute,
    onChange: (ReactionRoute) -> Unit,
    onRemove: () -> Unit,
) {
    // Saveable: an open card survives rotation and process death like the rest of the panel.
    var expanded by rememberSaveable { mutableStateOf(false) }
    var picking by remember { mutableStateOf<ReactionPick?>(null) }
    val sceneId = LocalSceneId.current
    Column(modifier = Modifier.padding(vertical = 4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.heightIn(min = 48.dp)) {
            Text(
                "${EventRecipe.sentence(route)} · ${route.mode.label} ${"%+.0f %%".format(route.amount * 100f)}",
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.weight(1f).clickable { expanded = !expanded },
            )
            Switch(checked = route.enabled, onCheckedChange = { onChange(route.copy(enabled = it)) })
        }
        if (route.target != LfoTarget.NONE && !ModTable.offeredTo(route.target, sceneId, ModTable.REACTION)) {
            ControlHint("${route.target.label} does nothing on this style.")
        }
        if (expanded) {
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                TextButton(onClick = { picking = ReactionPick.SOURCE }) {
                    Text(stringResource(R.string.customize_reaction_sound, route.source.label))
                }
                TextButton(onClick = { picking = ReactionPick.TARGET }) {
                    Text(stringResource(R.string.customize_moves, route.target.label))
                }
            }
            DropdownRow("Mode", ReactMode.entries.map { it.label }, ReactMode.entries.indexOf(route.mode)) {
                onChange(route.copy(mode = ReactMode.entries[it]))
            }
            LabeledSlider(REACT_DEPTH, route.amount, REACT_AMOUNT_RANGE, scope = ParamScope.UNIVERSAL, unit = ParamUnit.PERCENT) {
                onChange(route.copy(amount = it))
            }
            LabeledSlider(REACT_ATTACK, route.attackSeconds, 0f..1f, scope = ParamScope.UNIVERSAL, unit = ParamUnit.SECONDS) {
                onChange(route.copy(attackSeconds = it))
            }
            LabeledSlider(REACT_RELEASE, route.releaseSeconds, 0f..3f, scope = ParamScope.UNIVERSAL, unit = ParamUnit.SECONDS) {
                onChange(route.copy(releaseSeconds = it))
            }
            DropdownRow("Curve", ModCurve.entries.map { it.label }, ModCurve.entries.indexOf(route.curve)) {
                onChange(route.copy(curve = ModCurve.entries[it]))
            }
            TextButton(onClick = onRemove) { Text(stringResource(R.string.action_remove)) }
        }
    }
    picking?.let { which ->
        AlertDialog(
            onDismissRequest = { picking = null },
            title = {
                Text(stringResource(if (which == ReactionPick.SOURCE) R.string.customize_pick_sound else R.string.customize_pick_target))
            },
            text = {
                Column(modifier = Modifier.height(360.dp).verticalScroll(rememberScrollState())) {
                    if (which == ReactionPick.SOURCE) {
                        // The sound engine's sources, in the picker's sections (Levels, Drums, ...).
                        ReactSourceGroup.entries.forEach { group ->
                            Text(
                                group.label,
                                style = MaterialTheme.typography.labelLarge,
                                modifier = Modifier.padding(top = 8.dp, start = 12.dp),
                            )
                            ReactSource.entries.filter { it.group == group }.forEach { s ->
                                TextButton(onClick = {
                                    onChange(route.copy(source = s))
                                    picking = null
                                }) {
                                    val mark = if (s == route.source) "▶ " else ""
                                    Text(mark + s.label + if (s.isEvent) " (event)" else "")
                                }
                            }
                        }
                    } else {
                        LfoTarget.entries
                            .filter { it != LfoTarget.NONE && ModTable.offeredTo(it, sceneId, ModTable.REACTION) }
                            .forEach { t ->
                                TextButton(onClick = {
                                    onChange(route.copy(target = t))
                                    picking = null
                                }) { Text(if (t == route.target) "▶ ${t.label}" else t.label) }
                            }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { picking = null }) { Text(stringResource(R.string.action_close)) } },
        )
    }
}

@Composable
internal fun ModulatorCard(
    index: Int,
    config: LfoConfig,
    onChange: (LfoConfig) -> Unit,
) {
    var showTargetPicker by remember { mutableStateOf(false) }
    val sceneId = LocalSceneId.current
    Column(modifier = Modifier.padding(vertical = 6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                stringResource(R.string.customize_wave_n, index + 1),
                style = MaterialTheme.typography.titleSmall,
                modifier = Modifier.weight(1f),
            )
            Switch(checked = config.enabled, onCheckedChange = { onChange(config.copy(enabled = it)) })
        }
        if (config.enabled) {
            TextButton(onClick = { showTargetPicker = true }) { Text(stringResource(R.string.customize_moves, config.target.label)) }
            if (config.target != LfoTarget.NONE && !ModTable.offeredTo(config.target, sceneId, index)) {
                ControlHint("${config.target.label} does nothing on this style - pick another.")
            }
            DropdownRow("Follows", ModSource.entries.map { it.label }, ModSource.entries.indexOf(config.source)) {
                val source = ModSource.entries[it]
                onChange(config.copy(source = source, polarity = ModTable.naturalPolarity(source)))
            }
            if (config.source == ModSource.LFO) {
                DropdownRow("Wave shape", LfoWave.entries.map { it.label }, LfoWave.entries.indexOf(config.wave)) {
                    onChange(config.copy(wave = LfoWave.entries[it]))
                }
                val timing = listOf("Free") + LfoConfig.BEAT_CHOICES.map { if (it == 1) "1 beat" else "$it beats" }
                DropdownRow("Timing", timing, LfoConfig.BEAT_CHOICES.indexOf(config.beats) + 1) {
                    onChange(config.copy(beats = LfoConfig.BEAT_CHOICES.getOrElse(it - 1) { 0 }))
                }
                if (config.beats == 0) RateSecondsSlider(config.rateSeconds) { onChange(config.copy(rateSeconds = it)) }
            }
            LabeledSlider(MOD_DEPTH, config.depth, 0f..1f) { onChange(config.copy(depth = it)) }
            DropdownRow("Direction", ModPolarity.entries.map { it.label }, ModPolarity.entries.indexOf(config.polarity)) {
                onChange(config.copy(polarity = ModPolarity.entries[it]))
            }
            DropdownRow("Curve", ModCurve.entries.map { it.label }, ModCurve.entries.indexOf(config.curve)) {
                onChange(config.copy(curve = ModCurve.entries[it]))
            }
        }
    }
    if (showTargetPicker) {
        AlertDialog(
            onDismissRequest = { showTargetPicker = false },
            title = { Text(stringResource(R.string.customize_wave_moves, index + 1)) },
            text = {
                Column(modifier = Modifier.height(360.dp).verticalScroll(rememberScrollState())) {
                    LfoTarget.entries.forEach { t ->
                        // Native decides what this wave may move (a later wave, or what reaches the style).
                        if (ModTable.offeredTo(t, sceneId, index)) {
                            TextButton(onClick = {
                                onChange(config.copy(target = t))
                                showTargetPicker = false
                            }) { Text(if (t == config.target) "▶ ${t.label}" else t.label) }
                        }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { showTargetPicker = false }) { Text(stringResource(R.string.action_close)) } },
        )
    }
}

/**
 * A wave's speed, as the time for one cycle.
 *
 * Slid on a log scale because the useful span runs from a twentieth of a second to a minute, and
 * a linear track would spend nine tenths of its length between 6 s and 60 s.
 */
@Composable
private fun RateSecondsSlider(
    seconds: Float,
    onChange: (Float) -> Unit,
) {
    val lo = ModTable.minRateSeconds
    val hi = ModTable.maxRateSeconds
    val t = (ln(seconds.coerceIn(lo, hi) / lo) / ln(hi / lo)).coerceIn(0f, 1f)
    val shown = ParamFormat.format(seconds, ParamUnit.SECONDS)
    val cycleLabel = stringResource(R.string.customize_a11y_one_cycle)
    Column(Modifier.padding(vertical = 2.dp)) {
        Text(stringResource(R.string.customize_one_cycle_value, shown), style = MaterialTheme.typography.bodyMedium)
        Slider(
            value = t,
            onValueChange = { onChange(lo * (hi / lo).pow(it)) },
            valueRange = 0f..1f,
            // A thumb at 0 sits in the back-gesture strip; without the exclusion a drag from it leaves the screen.
            modifier =
                Modifier.fillMaxWidth().systemGestureExclusion().semantics {
                    contentDescription = cycleLabel
                    stateDescription = shown
                },
        )
    }
}

@Composable
internal fun AdsrCard(
    index: Int,
    config: AdsrConfig,
    onChange: (AdsrConfig) -> Unit,
) {
    val sceneId = LocalSceneId.current
    Column(modifier = Modifier.padding(vertical = 4.dp)) {
        CheckRow("Envelope ${index + 1} on", config.enabled) { onChange(config.copy(enabled = it)) }
        if (!config.enabled) return@Column
        // What it moves: one row and a checkable menu, in place of a chip per target and a "+" chip.
        // The ones already chosen stay listed even where this style ignores them, so they can be unticked.
        val offered =
            LfoTarget.entries.filter {
                it != LfoTarget.NONE && ModTable.offered(it) && (it in config.targets || ModTable.reaches(it, sceneId))
            }
        Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.envelope_moves), style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
            MultiPickButton(
                name = stringResource(R.string.envelope_moves_for, index + 1),
                summary = config.targets.joinToString { it.label }.ifEmpty { stringResource(R.string.envelope_moves_nothing) },
                picks = offered.map { MenuPick(it.label, it in config.targets) },
                modifier = Modifier.weight(1f, fill = false),
            ) { i ->
                val t = offered[i]
                onChange(config.copy(targets = if (t in config.targets) config.targets - t else config.targets + t))
            }
        }
        CardSubHeader(stringResource(R.string.customize_envelope_shape))
        LabeledSlider(ENV_ATTACK, config.attack, 0.005f..1f, unit = ParamUnit.SECONDS) { onChange(config.copy(attack = it)) }
        LabeledSlider(ENV_DECAY, config.decay, 0.01f..1.5f, unit = ParamUnit.SECONDS) { onChange(config.copy(decay = it)) }
        LabeledSlider(ENV_SUSTAIN, config.sustain, 0f..1f) { onChange(config.copy(sustain = it)) }
        LabeledSlider(ENV_RELEASE, config.release, 0.02f..2f, unit = ParamUnit.SECONDS) { onChange(config.copy(release = it)) }
        LabeledSlider(ENV_AMOUNT, config.amount, 0f..1.5f, unit = ParamUnit.PERCENT) { onChange(config.copy(amount = it)) }
        CardSubHeader(stringResource(R.string.customize_envelope_hold))
        ControlHint(
            stringResource(R.string.customize_envelope_hold_hint),
        )
        DropdownRow(ENV_GATE_SOURCE, EnvBand.entries.map { it.label }, EnvBand.entries.indexOf(config.band), lockKey = ENV_GATE_SOURCE) {
            onChange(config.copy(band = EnvBand.entries[it]))
        }
        LabeledSlider(ENV_GATE_LEVEL, config.gateThreshold, 0.05f..1f) { onChange(config.copy(gateThreshold = it)) }
        CheckRow(ENV_SUSTAIN_TRACK, config.sustainTrack) { onChange(config.copy(sustainTrack = it)) }
        CheckRow(ENV_RETRIGGER, config.retrigger) { onChange(config.copy(retrigger = it)) }
    }
}

/** Expert › Layers: a second style drawn under the active one. */
@Composable
internal fun LayersPage() {
    val layers by LayersBus.state.collectAsStateWithLifecycle()
    val layerScenes by LayersBus.availableScenes.collectAsStateWithLifecycle()
    val activeScene by LayersBus.activeSceneId.collectAsStateWithLifecycle()
    ControlHint(
        stringResource(R.string.customize_layers_hint),
    )
    CheckRow(LAYERS_ENABLED, layers.enabled, scope = ParamScope.UNIVERSAL) { on ->
        val scene = layers.sceneId ?: layerScenes.firstOrNull { it != activeScene }
        LayersBus.state.value = layers.copy(enabled = on, sceneId = scene)
    }
    if (!layers.enabled || !sectionVisible()) return
    var showLayerPicker by remember { mutableStateOf(false) }
    Box {
        TextButton(onClick = { showLayerPicker = true }) {
            val layerStyle = layers.sceneId?.let { sceneDisplayLabel(it) } ?: stringResource(R.string.customize_layer_none)
            Text(stringResource(R.string.customize_layer_style, layerStyle))
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
        ControlHint(stringResource(R.string.customize_layer_idle_hint))
    }
    val blendNames = BlendMode.entries.map { mode -> mode.name.lowercase().replaceFirstChar { it.uppercase() } }
    DropdownRow("Blend", blendNames, BlendMode.entries.indexOf(layers.blend)) {
        LayersBus.state.value = layers.copy(blend = BlendMode.entries[it])
    }
    LabeledSlider(LAYER_MIX, layers.mix, 0f..1f, scope = ParamScope.UNIVERSAL) { LayersBus.state.value = layers.copy(mix = it) }
}

/**
 * One Note & key strength: a 0..2 slider (1 = as designed; the range is the schema's) with its one-line hint under it. The key is
 * the shown name, since these are not catalogue parameters: they have no style scope (every style hears
 * notes) and the randomizer never rolls them, so the scope and unit are given rather than looked up.
 */
@Composable
private fun NoteKeySlider(
    @StringRes name: Int,
    @StringRes hint: Int,
    p: SceneParams,
    param: KProperty1<SceneParams, Float>,
    onChange: (Float) -> Unit,
) {
    val label = stringResource(name)
    LabeledSlider(label, p, param, scope = ParamScope.UNIVERSAL, unit = ParamUnit.TIMES, onChange = onChange)
    if (visible(label, ParamScope.UNIVERSAL)) ControlHint(stringResource(hint))
}

/**
 * Music › Notes & key: how strongly each part of the musical layer (core/viz/NoteResponse) feeds this
 * style's own knobs, saved with the style like every Customize value. The switch that runs the layer at all is
 * Settings › Sound reaction (free, on by default); with it off these sliders do nothing, so the page says so
 * rather than letting a user drag a dead control.
 */
@Composable
internal fun NoteKeyPage(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val context = LocalContext.current
    val running by remember { NoteResponseStore.get(context) }.enabled.collectAsStateWithLifecycle()
    if (!running && sectionVisible()) ControlHint(stringResource(R.string.note_key_off))
    ControlHint(stringResource(R.string.note_key_intro))
    NoteKeySlider(R.string.note_key_pitch, R.string.note_key_pitch_hint, p, SceneParams::notePitch) {
        onChange(p.copy(notePitch = it))
    }
    NoteKeySlider(R.string.note_key_intervals, R.string.note_key_intervals_hint, p, SceneParams::noteIntervals) {
        onChange(p.copy(noteIntervals = it))
    }
    NoteKeySlider(R.string.note_key_tension, R.string.note_key_tension_hint, p, SceneParams::noteTension) {
        onChange(p.copy(noteTension = it))
    }
    NoteKeySlider(R.string.note_key_chords, R.string.note_key_chords_hint, p, SceneParams::noteChords) {
        onChange(p.copy(noteChords = it))
    }
    NoteKeySlider(R.string.note_key_drums, R.string.note_key_drums_hint, p, SceneParams::noteDrums) {
        onChange(p.copy(noteDrums = it))
    }
}

private const val TRANSITION_CHIP_LIMIT = 40

/** Native's transition_choice::kRandomId. */
private const val RANDOM_TRANSITION = "random"

/**
 * Effects' transition style: how one style hands over to the next. The built-in transitions,
 * then the gl-transitions library behind a search. Nothing while the renderer has no transition yet.
 */
@Composable
internal fun TransitionPicker(
    transitionId: String?,
    onTransitionId: (String) -> Unit,
) {
    if (transitionId == null || !sectionVisible()) return
    val ctx = LocalContext.current
    val library = remember { TransitionCatalog.library(ctx) }
    // "cut" is left out: VisualSafety always plays it as a fade, so its entry lied. "random" draws one
    // per change, never the same twice running (native core/viz/TransitionChoice, MilkDrop's habit).
    val builtIn = remember { TransitionCatalog.BUILT_IN_IDS.filter { it != "cut" } + RANDOM_TRANSITION }
    DropdownRow(
        stringResource(R.string.transition_built_in),
        builtIn,
        builtIn.indexOf(transitionId),
        placeholder = stringResource(R.string.transition_from_library),
    ) { onTransitionId(builtIn[it]) }
    if (library.isEmpty()) return
    ControlHint(
        "${library.size} more from the gl-transitions library. Each blends the outgoing and incoming styles " +
            "with every effect already applied, so nothing pops off during a switch.",
    )
    var query by remember { mutableStateOf("") }
    OutlinedTextField(
        value = query,
        onValueChange = { query = it },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true,
        label = { Text(stringResource(R.string.customize_search_transitions)) },
    )
    val shown = remember(query, library) { if (query.isBlank()) library else library.filter { it.name.contains(query, ignoreCase = true) } }
    val names = shown.take(TRANSITION_CHIP_LIMIT).map { it.name }
    DropdownRow(
        stringResource(R.string.transition_library),
        names,
        names.indexOf(transitionId),
        placeholder = stringResource(R.string.transition_library_pick, shown.size),
    ) { onTransitionId(names[it]) }
    if (shown.size > TRANSITION_CHIP_LIMIT) {
        ControlHint(stringResource(R.string.customize_transitions_more, shown.size - TRANSITION_CHIP_LIMIT))
    }
}

/** The hot-phone line for the renderer's liquid mode (0 off, 1 full, 2 lite, 3 warp, 4 paused), or null. */
@Composable
internal fun liquidHotNote(liquidMode: Int): String? =
    when (liquidMode) {
        2, 3 -> stringResource(R.string.customize_liquid_hot_simplified)
        4 -> stringResource(R.string.customize_liquid_hot_paused)
        else -> null
    }

/**
 * Expert › Shader code: the custom fluid shaders (force and dye) on Fluid. The style's own shader box
 * is no longer offered here (the owner's call, 2026-11-11); native's map shows this section on Fluid
 * only (`core/viz/CustomizeMap`, Show::ShaderCode), and a look's saved custom shader still applies.
 */
@Composable
internal fun ShaderCodePage(host: AdvancedHost) {
    val viz by host.viewModel.vizState.collectAsStateWithLifecycle()
    if (SceneCapabilities.isFluid(viz.sceneId)) {
        InjectionShaderEditors(viz.shaderError) { force, dye ->
            host.visualizerView.visualizerRenderer.submitFluidInjectionShaders(force, dye)
        }
    }
}

@Composable
private fun InjectionShaderEditors(
    injectionError: String?,
    onApplyInjectionShaders: (String?, String?) -> Unit,
) {
    SectionHeader(stringResource(R.string.customize_fluid_shaders_title))
    ControlHint(
        stringResource(R.string.customize_fluid_shaders_hint),
    )
    val ctx = LocalContext.current
    // The asset read is disk I/O, so it runs on IO and the editors wait for it (no empty-editor flash).
    val template by produceState<String?>(initialValue = null, ctx) {
        value =
            withContext(Dispatchers.IO) {
                FluidShaderTemplate.splat(ctx).also {
                    if (it.isEmpty()) RingLog.note("CustomizeAdvanced", "fluid splat asset read failed")
                }
            }
    }
    val builtIn = template ?: return
    var forceSrc by remember(builtIn) { mutableStateOf(builtIn) }
    var dyeSrc by remember(builtIn) { mutableStateOf(builtIn) }
    var editorsUsed by remember { mutableStateOf(false) }
    Text(stringResource(R.string.customize_fluid_force_shader), style = MaterialTheme.typography.bodyMedium)
    OutlinedTextField(
        value = forceSrc,
        onValueChange = {
            forceSrc = it
            editorsUsed = true
        },
        modifier = Modifier.fillMaxWidth().height(180.dp),
        textStyle = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
    )
    Text(stringResource(R.string.customize_fluid_dye_shader), style = MaterialTheme.typography.bodyMedium)
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
        Text(injectionError, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        TextButton(onClick = {
            val f = forceSrc.takeIf { editorsUsed && it.isNotBlank() && it != builtIn }
            val d = dyeSrc.takeIf { editorsUsed && it.isNotBlank() && it != builtIn }
            onApplyInjectionShaders(f, d)
        }) { Text(stringResource(R.string.customize_fluid_shaders_apply)) }
        TextButton(onClick = {
            forceSrc = builtIn
            dyeSrc = builtIn
            onApplyInjectionShaders(null, null)
        }) { Text(stringResource(R.string.customize_fluid_shaders_builtin)) }
    }
}

// Names for the controls that are panel machinery rather than scene parameters. They live in one
// place so the search matches them and nothing has to spell a string twice.
private const val MOD_DEPTH = "Wave depth"
private const val REACT_DEPTH = "Amount"
private const val REACT_ATTACK = "Rise time"
private const val REACT_RELEASE = "Fall time"
private const val ENV_ATTACK = "Envelope rise"
private const val ENV_DECAY = "Envelope fall"
private const val ENV_SUSTAIN = "Sustain"
private const val ENV_RELEASE = "Release"
private const val ENV_AMOUNT = "Envelope amount"
private const val ENV_GATE_SOURCE = "Hold on"
private const val ENV_GATE_LEVEL = "Hold level"
private const val ENV_SUSTAIN_TRACK = "Sustain follows the sound"
private const val ENV_RETRIGGER = "Restart on every hit"
private const val LAYERS_ENABLED = "Layers enabled"
private const val LAYER_MIX = "Layer mix"
