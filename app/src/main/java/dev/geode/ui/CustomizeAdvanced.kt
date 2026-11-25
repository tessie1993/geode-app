@file:Suppress("MatchingDeclarationName")

package dev.geode.ui

import android.content.Context
import androidx.annotation.StringRes
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
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
import dev.geode.engine.bridge.CatalogueNative
import dev.geode.engine.bridge.CustomizeNative
import dev.geode.engine.bridge.ModulationNative
import dev.geode.engine.bridge.ParamsNative
import dev.geode.engine.bridge.VizNative
import dev.geode.geodeContainer
import dev.geode.model.ParamScope
import dev.geode.model.SceneParams
import dev.geode.model.StyleCatalogue
import dev.geode.stage.StageView
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.UiSound
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

/** What a deeper page edits, gathered once so each page reads plain values. */
internal class AdvancedHost(
    val viewModel: PlayerViewModel,
    val visualsViewModel: VisualsViewModel,
    val visualizerView: StageView,
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
    reactions: String?,
    handMask: Int,
    onEdit: ReactionsEdit,
    onReroll: () -> Unit,
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
        remember(sceneId, p.int("reactSeed"), reactions, handMask) {
            ModulationNative.modRoutesPlaying(sceneId, p.int("reactSeed"), reactions, handMask)
        }
    val each = ModulationNative.MOD_ROUTE_VALUES
    val count = routes.size / each
    val sounds = LocalUiSounds.current
    val tap: (() -> Unit) -> Unit = { action ->
        sounds.play(UiSound.TAP)
        action()
    }
    FlowRow(horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
        FilledTonalButton(onClick = { tap(onReroll) }) { Text(stringResource(R.string.customize_reactions_shuffle)) }
        if (reactions != null) {
            FilledTonalButton(onClick = { tap { onEdit(ModulationNative.MOD_EDIT_CLEAR, 0, 0, 0f) } }) {
                Text(stringResource(R.string.customize_reactions_use_style))
            }
        }
        if (count < ModCatalog.count("routes")) {
            FilledTonalButton(onClick = { tap { onEdit(ModulationNative.MOD_EDIT_ROUTE_ADD, 0, 0, 0f) } }) {
                Text(stringResource(R.string.action_add))
            }
        }
    }
    repeat(count) { i ->
        ReactionCard(
            route = routes.copyOfRange(i * each, (i + 1) * each),
            onSet = { field, value -> onEdit(ModulationNative.MOD_EDIT_ROUTE_SET, i, field, value) },
            onRemove = { onEdit(ModulationNative.MOD_EDIT_ROUTE_REMOVE, i, 0, 0f) },
        )
    }
}

private fun FloatArray.intAt(i: Int): Int = getOrElse(i) { 0f }.toInt()

private fun FloatArray.flagAt(i: Int): Boolean = getOrElse(i) { 0f } > 0.5f

private fun Boolean.wire(): Float = if (this) 1f else 0f

/** A picker over one [kind] of native's words: the row lists their labels and sends the picked ordinal. */
@Composable
private fun WordRow(
    name: String,
    kind: String,
    ordinal: Int,
    lockKey: String? = null,
    onPick: (Float) -> Unit,
) {
    val words = ModCatalog.words(kind)
    DropdownRow(name, words.map { it.label }, words.indexOfFirst { it.ordinal == ordinal }, lockKey = lockKey) {
        onPick(words[it].ordinal.toFloat())
    }
}

private enum class ReactionPick { SOURCE, TARGET }

/** One route as native reads it ([ModulationNative.MOD_ROUTE_VALUES] fields); [onSet] sends one field's new value. */
@Composable
private fun ReactionCard(
    route: FloatArray,
    onSet: (field: Int, value: Float) -> Unit,
    onRemove: () -> Unit,
) {
    // Saveable: an open card survives rotation and process death like the rest of the panel.
    var expanded by rememberSaveable { mutableStateOf(false) }
    var picking by remember { mutableStateOf<ReactionPick?>(null) }
    val sceneId = LocalSceneId.current
    val source = route.intAt(ModulationNative.MOD_ROUTE_SOURCE)
    val target = route.intAt(ModulationNative.MOD_ROUTE_TARGET)
    val amount = route.getOrElse(ModulationNative.MOD_ROUTE_AMOUNT) { 0f }
    // The whole range native accepts: the style's own reactions draw drop surges above 1. Below zero, so signed.
    val amountRange = ModCatalog.range("route.amount")
    val amountText =
        remember(amount, amountRange) {
            ParamsNative.paramText("", ParamsNative.PARAM_UNIT_PERCENT, amount, amountRange.start, amountRange.endInclusive, Float.NaN)
        }
    val sourceLabel = ModCatalog.label("react", source)
    val targetLabel = ModCatalog.label("target", target)
    val modeLabel = ModCatalog.label("mode", route.intAt(ModulationNative.MOD_ROUTE_MODE))
    Column(modifier = Modifier.padding(vertical = 4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.heightIn(min = 48.dp)) {
            Text(
                panelFormat("customize_reaction_summary", sourceLabel, targetLabel, modeLabel, amountText),
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.weight(1f).clickable { expanded = !expanded },
            )
            Switch(
                checked = route.flagAt(ModulationNative.MOD_ROUTE_ENABLED),
                onCheckedChange = { onSet(ModulationNative.MOD_ROUTE_ENABLED, it.wire()) },
            )
        }
        if (target != 0 &&
            !route.flagAt(ModulationNative.MOD_ROUTE_REACHES)
        ) {
            ControlHint(panelFormat("customize_target_inert", targetLabel))
        }
        if (expanded) {
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                TextButton(onClick = { picking = ReactionPick.SOURCE }) {
                    Text(stringResource(R.string.customize_reaction_sound, sourceLabel))
                }
                TextButton(onClick = { picking = ReactionPick.TARGET }) {
                    Text(stringResource(R.string.customize_moves, targetLabel))
                }
            }
            WordRow("Mode", "mode", route.intAt(ModulationNative.MOD_ROUTE_MODE)) { onSet(ModulationNative.MOD_ROUTE_MODE, it) }
            LabeledSlider(REACT_DEPTH, amount, amountRange, scope = ParamScope.UNIVERSAL, unit = ParamsNative.PARAM_UNIT_PERCENT) {
                onSet(ModulationNative.MOD_ROUTE_AMOUNT, it)
            }
            LabeledSlider(
                REACT_ATTACK,
                route.getOrElse(ModulationNative.MOD_ROUTE_ATTACK_SECONDS) { 0f },
                ModCatalog.range("route.attackSeconds"),
                scope = ParamScope.UNIVERSAL,
                unit = ParamsNative.PARAM_UNIT_SECONDS,
            ) { onSet(ModulationNative.MOD_ROUTE_ATTACK_SECONDS, it) }
            LabeledSlider(
                REACT_RELEASE,
                route.getOrElse(ModulationNative.MOD_ROUTE_RELEASE_SECONDS) { 0f },
                ModCatalog.range("route.releaseSeconds"),
                scope = ParamScope.UNIVERSAL,
                unit = ParamsNative.PARAM_UNIT_SECONDS,
            ) { onSet(ModulationNative.MOD_ROUTE_RELEASE_SECONDS, it) }
            WordRow("Curve", "curve", route.intAt(ModulationNative.MOD_ROUTE_CURVE)) { onSet(ModulationNative.MOD_ROUTE_CURVE, it) }
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
                        val sources = ModCatalog.words("react")
                        ModCatalog.words("group").forEach { group ->
                            Text(
                                group.label,
                                style = MaterialTheme.typography.labelLarge,
                                modifier = Modifier.padding(top = 8.dp, start = 12.dp),
                            )
                            sources.filter { it.more.firstOrNull() == group.name }.forEach { s ->
                                TextButton(onClick = {
                                    onSet(ModulationNative.MOD_ROUTE_SOURCE, s.ordinal.toFloat())
                                    picking = null
                                }) {
                                    val mark = if (s.ordinal == source) "▶ " else ""
                                    Text(mark + s.label + if (s.more.getOrNull(1) == "1") " (event)" else "")
                                }
                            }
                        }
                    } else {
                        // Native lists what a route may move on this style.
                        val offered =
                            remember(sceneId) { ModulationNative.modTargetsOffered(sceneId, ModulationNative.MOD_PICKER_REACTION) }
                        offered.forEach { t ->
                            val label = ModCatalog.label("target", t)
                            TextButton(onClick = {
                                onSet(ModulationNative.MOD_ROUTE_TARGET, t.toFloat())
                                picking = null
                            }) { Text(if (t == target) "▶ $label" else label) }
                        }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { picking = null }) { Text(stringResource(R.string.action_close)) } },
        )
    }
}

/** One wave of the saved [lfos], drawn from native's reading of it; [onChange] sends a `ModulationNative.MOD_LFO_*` field's new value. */
@Composable
internal fun ModulatorCard(
    index: Int,
    lfos: String,
    onChange: (field: Int, value: Float) -> Unit,
) {
    var showTargetPicker by remember { mutableStateOf(false) }
    val sceneId = LocalSceneId.current
    val v = remember(lfos, index, sceneId) { ModulationNative.modLfoValues(lfos, index, sceneId) }
    // Native says which controls the slot reads as it stands: shape and timing, its own period, a knob.
    val shows = remember(lfos, index) { ModulationNative.modLfoShows(lfos, index) }
    val target = v.intAt(ModulationNative.MOD_LFO_TARGET)
    val targetLabel = ModCatalog.label("target", target)
    Column(modifier = Modifier.padding(vertical = 6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                stringResource(R.string.customize_wave_n, index + 1),
                style = MaterialTheme.typography.titleSmall,
                modifier = Modifier.weight(1f),
            )
            Switch(
                checked = v.flagAt(ModulationNative.MOD_LFO_ENABLED),
                onCheckedChange = { onChange(ModulationNative.MOD_LFO_ENABLED, it.wire()) },
            )
        }
        if (v.flagAt(ModulationNative.MOD_LFO_ENABLED)) {
            TextButton(onClick = { showTargetPicker = true }) { Text(stringResource(R.string.customize_moves, targetLabel)) }
            if (target != 0 && !v.flagAt(ModulationNative.MOD_LFO_REACHES)) {
                ControlHint(panelFormat("customize_target_inert_pick", targetLabel))
            }
            if (shows and ModulationNative.MOD_SHOWS_KNOB != 0) {
                MacroKnobRow(v.intAt(ModulationNative.MOD_LFO_KNOB)) { onChange(ModulationNative.MOD_LFO_KNOB, it.toFloat()) }
            }
            // Native also turns the wave the way a new source is useful.
            WordRow("Follows", "source", v.intAt(ModulationNative.MOD_LFO_SOURCE)) { onChange(ModulationNative.MOD_LFO_SOURCE, it) }
            if (shows and ModulationNative.MOD_SHOWS_WAVE != 0) {
                WordRow("Wave shape", "wave", v.intAt(ModulationNative.MOD_LFO_WAVE)) { onChange(ModulationNative.MOD_LFO_WAVE, it) }
                val timing = ModCatalog.words("beats")
                val beats = v.intAt(ModulationNative.MOD_LFO_BEATS)
                DropdownRow("Timing", timing.map { it.label }, timing.indexOfFirst { it.name.toIntOrNull() == beats }) {
                    onChange(ModulationNative.MOD_LFO_BEATS, timing[it].name.toFloatOrNull() ?: 0f)
                }
                val moment = v.intAt(ModulationNative.MOD_LFO_MOMENT)
                MomentTriggerRow(
                    moment,
                    stringResource(R.string.mod_moment_free),
                ) { onChange(ModulationNative.MOD_LFO_MOMENT, it.toFloat()) }
                val rate = v.getOrElse(ModulationNative.MOD_LFO_RATE_SECONDS) { 0f }
                if (shows and ModulationNative.MOD_SHOWS_RATE !=
                    0
                ) {
                    RateSecondsSlider(rate) { onChange(ModulationNative.MOD_LFO_RATE_SECONDS, it) }
                }
            }
            LabeledSlider(MOD_DEPTH, v.getOrElse(ModulationNative.MOD_LFO_DEPTH) { 0f }, ModCatalog.range("lfo.depth")) {
                onChange(ModulationNative.MOD_LFO_DEPTH, it)
            }
            WordRow("Direction", "polarity", v.intAt(ModulationNative.MOD_LFO_POLARITY)) { onChange(ModulationNative.MOD_LFO_POLARITY, it) }
            WordRow("Curve", "curve", v.intAt(ModulationNative.MOD_LFO_CURVE)) { onChange(ModulationNative.MOD_LFO_CURVE, it) }
        }
    }
    if (showTargetPicker) {
        AlertDialog(
            onDismissRequest = { showTargetPicker = false },
            title = { Text(stringResource(R.string.customize_wave_moves, index + 1)) },
            text = {
                Column(modifier = Modifier.height(360.dp).verticalScroll(rememberScrollState())) {
                    // Native decides what this wave may move (a later wave, or what reaches the style).
                    val offered = remember(sceneId, index) { ModulationNative.modTargetsOffered(sceneId, index) }
                    offered.forEach { t ->
                        val label = ModCatalog.label("target", t)
                        TextButton(onClick = {
                            onChange(ModulationNative.MOD_LFO_TARGET, t.toFloat())
                            showTargetPicker = false
                        }) { Text(if (t == target) "▶ $label" else label) }
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
    val range = ModCatalog.range("lfo.rateSeconds")
    val lo = range.start
    val hi = range.endInclusive
    val t = remember(seconds, lo, hi) { ParamsNative.sliderLogPosition(seconds, lo, hi) }
    val shown = remember(seconds) { ParamsNative.paramText("", ParamsNative.PARAM_UNIT_SECONDS, seconds, 0f, 0f, Float.NaN) }
    val cycleLabel = stringResource(R.string.customize_a11y_one_cycle)
    Column(Modifier.padding(vertical = 2.dp)) {
        Text(stringResource(R.string.customize_one_cycle_value, shown), style = MaterialTheme.typography.bodyMedium)
        Slider(
            value = t,
            onValueChange = { onChange(ParamsNative.sliderLogValue(it, lo, hi)) },
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

/** One envelope of the saved [adsrs], drawn from native's reading of it; [onChange] sends a `ModulationNative.MOD_ADSR_*` field's new value. */
@Composable
internal fun AdsrCard(
    index: Int,
    adsrs: String,
    onChange: (field: Int, value: Float) -> Unit,
) {
    val sceneId = LocalSceneId.current
    val v = remember(adsrs, index) { ModulationNative.modAdsrValues(adsrs, index) }
    Column(modifier = Modifier.padding(vertical = 4.dp)) {
        CheckRow(
            "Envelope ${index + 1} on",
            v.flagAt(ModulationNative.MOD_ADSR_ENABLED),
        ) { onChange(ModulationNative.MOD_ADSR_ENABLED, it.wire()) }
        if (!v.flagAt(ModulationNative.MOD_ADSR_ENABLED)) return@Column
        // What it moves: one row and a checkable menu, in place of a chip per target and a "+" chip.
        // The ones already chosen stay listed even where this style ignores them, so they can be unticked (native's list).
        val targets = v.drop(ModulationNative.MOD_ADSR_VALUES).map { it.toInt() }
        val offered = remember(adsrs, index, sceneId) { ModulationNative.modAdsrTargetsOffered(adsrs, index, sceneId) }
        Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.envelope_moves), style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
            MultiPickButton(
                name = stringResource(R.string.envelope_moves_for, index + 1),
                summary =
                    targets.joinToString { ModCatalog.label("target", it) }.ifEmpty { stringResource(R.string.envelope_moves_nothing) },
                picks = offered.map { MenuPick(ModCatalog.label("target", it), it in targets) },
                modifier = Modifier.weight(1f, fill = false),
            ) { i -> onChange(ModulationNative.MOD_ADSR_TARGET, offered[i].toFloat()) }
        }
        val shows = remember(adsrs, index) { ModulationNative.modAdsrShows(adsrs, index) }
        if (shows and ModulationNative.MOD_SHOWS_KNOB != 0) {
            MacroKnobRow(v.intAt(ModulationNative.MOD_ADSR_KNOB)) { onChange(ModulationNative.MOD_ADSR_KNOB, it.toFloat()) }
        }
        CardSubHeader(stringResource(R.string.customize_envelope_shape))
        AdsrSlider(ENV_ATTACK, v, ModulationNative.MOD_ADSR_ATTACK, "adsr.attack", ParamsNative.PARAM_UNIT_SECONDS, onChange)
        AdsrSlider(ENV_DECAY, v, ModulationNative.MOD_ADSR_DECAY, "adsr.decay", ParamsNative.PARAM_UNIT_SECONDS, onChange)
        AdsrSlider(ENV_SUSTAIN, v, ModulationNative.MOD_ADSR_SUSTAIN, "adsr.sustain", ParamsNative.PARAM_UNIT_OWN, onChange)
        AdsrSlider(ENV_RELEASE, v, ModulationNative.MOD_ADSR_RELEASE, "adsr.release", ParamsNative.PARAM_UNIT_SECONDS, onChange)
        AdsrSlider(ENV_AMOUNT, v, ModulationNative.MOD_ADSR_AMOUNT, "adsr.amount", ParamsNative.PARAM_UNIT_PERCENT, onChange)
        CardSubHeader(stringResource(R.string.customize_envelope_hold))
        ControlHint(
            stringResource(R.string.customize_envelope_hold_hint),
        )
        WordRow(ENV_GATE_SOURCE, "band", v.intAt(ModulationNative.MOD_ADSR_BAND), lockKey = ENV_GATE_SOURCE) {
            onChange(ModulationNative.MOD_ADSR_BAND, it)
        }
        AdsrSlider(ENV_GATE_LEVEL, v, ModulationNative.MOD_ADSR_GATE_THRESHOLD, "adsr.gateThreshold", ParamsNative.PARAM_UNIT_OWN, onChange)
        CheckRow(ENV_SUSTAIN_TRACK, v.flagAt(ModulationNative.MOD_ADSR_SUSTAIN_TRACK)) {
            onChange(ModulationNative.MOD_ADSR_SUSTAIN_TRACK, it.wire())
        }
        CheckRow(ENV_RETRIGGER, v.flagAt(ModulationNative.MOD_ADSR_RETRIGGER)) { onChange(ModulationNative.MOD_ADSR_RETRIGGER, it.wire()) }
        MomentTriggerRow(v.intAt(ModulationNative.MOD_ADSR_MOMENT), stringResource(R.string.mod_moment_note_start)) {
            onChange(ModulationNative.MOD_ADSR_MOMENT, it.toFloat())
        }
    }
}

/** One envelope slider: the value at [field] of [values], over the reach native publishes as [range]. */
@Composable
private fun AdsrSlider(
    label: String,
    values: FloatArray,
    field: Int,
    range: String,
    unit: Int,
    onChange: (field: Int, value: Float) -> Unit,
) {
    LabeledSlider(label, values.getOrElse(field) { 0f }, ModCatalog.range(range), unit = unit) { onChange(field, it) }
}

/**
 * Expert › Layers: a second style drawn over the active one. Native keeps the layer, process-wide, and decides which
 * style it draws and everything this page offers (core/viz/LayerState, core/viz/LayerPick); the page draws the values
 * and lists native hands it, hands each choice back, and saves the line native returns ([dev.geode.data.LookStore]).
 */
@Composable
internal fun LayersPage() {
    val looks = LocalContext.current.geodeContainer.looks
    val active = LocalSceneId.current
    var edits by remember { mutableIntStateOf(0) }
    // Read again now and then too: what Auto Director or a knob draws moves without an edit here.
    // The first value read once, not on every pass: produceState keeps only its first initial value anyway.
    val shown by produceState(remember { VizNative.layerShown(active) }, active, edits) {
        while (true) {
            value = VizNative.layerShown(active)
            delay(LAYER_SHOWN_POLL_MS)
        }
    }
    // The style picker's family as the user browses it; -1 is native's, the one holding the style drawn now.
    var family by remember(shown) { mutableIntStateOf(-1) }
    val values = remember(active, family, edits, shown) { VizNative.layerValues(active, family) }
    val edit = { field: Int, value: Float, styleId: String ->
        VizNative.layerEdit(field, value, styleId).takeIf { it.isNotEmpty() }?.let(looks::saveLayer)
        edits += 1
    }
    val list = { which: Int -> VizNative.layerList(which, active, values[VizNative.LAYER_FAMILY].toInt()).asList() }
    ControlHint(stringResource(R.string.customize_layers_hint))
    val on = values[VizNative.LAYER_ON] > 0f
    CheckRow(LAYERS_ENABLED, on, scope = ParamScope.UNIVERSAL) { edit(VizNative.LAYER_ON, if (it) 1f else 0f, "") }
    if (!on || !sectionVisible()) return
    val source = values[VizNative.LAYER_SOURCE].toInt()
    val sources = remember(active, values) { list(VizNative.LAYER_LIST_SOURCES) }
    DropdownRow(stringResource(R.string.customize_layer_from), sources, source) {
        edit(VizNative.LAYER_SOURCE, it.toFloat(), "")
    }
    when (source) {
        VizNative.LAYER_SOURCE_USER -> {
            val families = remember(active, values) { list(VizNative.LAYER_LIST_FAMILIES).map { CustomizeNative.styleFamilyLabel(it) } }
            val styles = remember(active, values) { list(VizNative.LAYER_LIST_STYLES) }
            val styleLabels = remember(styles) { styles.map { CustomizeNative.styleLabel(it) } }
            DropdownRow(stringResource(R.string.customize_layer_family), families, values[VizNative.LAYER_FAMILY].toInt()) { family = it }
            DropdownRow(
                stringResource(R.string.customize_layer_pick),
                styleLabels,
                values[VizNative.LAYER_STYLE].toInt(),
                placeholder = stringResource(R.string.customize_layer_suits),
            ) { edit(VizNative.LAYER_STYLE, 0f, styles[it]) }
            if (values[VizNative.LAYER_PICKED] > 0f) {
                TextButton(onClick = { edit(VizNative.LAYER_STYLE, 0f, "") }) { Text(stringResource(R.string.customize_layer_suits)) }
            }
        }
        VizNative.LAYER_SOURCE_DIRECTOR -> ControlHint(stringResource(R.string.customize_layer_director_hint))
        VizNative.LAYER_SOURCE_MACRO -> {
            val knob = values[VizNative.LAYER_KNOB].toInt()
            val knobs = remember(active, values) { list(VizNative.LAYER_LIST_KNOBS) }
            DropdownRow(stringResource(R.string.customize_layer_knob), knobs, knob) {
                edit(VizNative.LAYER_KNOB, it.toFloat(), "")
            }
            ControlHint(stringResource(R.string.customize_layer_macro_hint))
        }
    }
    if (shown.isNotEmpty()) ControlHint(stringResource(R.string.customize_layer_style, sceneDisplayLabel(shown)))
    val blend = values[VizNative.LAYER_BLEND].toInt()
    val blends = remember(active, values) { list(VizNative.LAYER_LIST_BLENDS) }
    DropdownRow("Blend", blends, blend) { edit(VizNative.LAYER_BLEND, it.toFloat(), "") }
    LabeledSlider(LAYER_MIX, values[VizNative.LAYER_MIX], 0f..1f, scope = ParamScope.UNIVERSAL) { edit(VizNative.LAYER_MIX, it, "") }
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
    param: String,
    onChange: (SceneParams) -> Unit,
) {
    val label = stringResource(name)
    LabeledSlider(label, p, param, scope = ParamScope.UNIVERSAL, unit = ParamsNative.PARAM_UNIT_TIMES, onChange = onChange)
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
    val running by remember { context.geodeContainer.noteResponse }.enabled.collectAsStateWithLifecycle()
    if (!running && sectionVisible()) ControlHint(stringResource(R.string.note_key_off))
    ControlHint(stringResource(R.string.note_key_intro))
    NoteKeySlider(R.string.note_key_pitch, R.string.note_key_pitch_hint, p, "notePitch", onChange)
    NoteKeySlider(R.string.note_key_intervals, R.string.note_key_intervals_hint, p, "noteIntervals", onChange)
    NoteKeySlider(R.string.note_key_tension, R.string.note_key_tension_hint, p, "noteTension", onChange)
    NoteKeySlider(R.string.note_key_chords, R.string.note_key_chords_hint, p, "noteChords", onChange)
    NoteKeySlider(R.string.note_key_drums, R.string.note_key_drums_hint, p, "noteDrums", onChange)
}

private const val TRANSITION_CHIP_LIMIT = 40

/** What the transition picker lists, as native hands it over: the built-in ids it offers, then the library's names. */
private class TransitionChoices(
    val builtIn: List<String>,
    val library: List<String>,
)

/** Native's picker table (`core/viz/TransitionLibrary`, `encodeTransitionPicker`): two lists, split on RS then US. */
private fun transitionChoices(context: Context): TransitionChoices {
    val table = runCatching { String(CatalogueNative.transitionPicker(context.assets), Charsets.UTF_8) }.getOrDefault("")
    val lists = table.split('\u001e')

    fun list(i: Int) =
        lists
            .getOrNull(i)
            .orEmpty()
            .split('\u001f')
            .filter { it.isNotEmpty() }

    return TransitionChoices(builtIn = list(0), library = list(1))
}

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
    // Native leaves "cut" out (VisualSafety plays it as a fade) and offers "random", one per change, never
    // the same twice running (core/viz/TransitionChoice).
    val choices = remember { transitionChoices(ctx) }
    val library = choices.library
    val builtIn = choices.builtIn
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
    val shown = remember(query, library) { library.filter { CustomizeNative.customizeMatches(query, it) } }
    val names = shown.take(TRANSITION_CHIP_LIMIT)
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

/**
 * Expert › Shader code: the custom fluid shaders (force and dye) on Fluid. The style's own shader box
 * is no longer offered here (the owner's call, 2026-11-11); native's map shows this section on Fluid
 * only (`core/viz/CustomizeMap`, Show::ShaderCode), and a look's saved custom shader still applies.
 */
@Composable
internal fun ShaderCodePage(host: AdvancedHost) {
    val viz by host.viewModel.vizState.collectAsStateWithLifecycle()
    if (StyleCatalogue.isFluid(viz.sceneId)) {
        InjectionShaderEditors(viz.shaderError) { force, dye ->
            host.visualizerView.stage.submitFluidInjectionShaders(force, dye)
        }
    }
}

/** The built-in splat shader the two editors open on; native compiles what the user sends back. */
private const val FLUID_SPLAT_ASSET = "shaders/fluid_splat_frag.glsl"

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
                runCatching {
                    ctx.assets
                        .open(FLUID_SPLAT_ASSET)
                        .bufferedReader()
                        .use { it.readText() }
                }.getOrDefault("").also {
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

// How often Customize › Layers reads again what Auto Director or a knob draws.
private const val LAYER_SHOWN_POLL_MS = 1000L
