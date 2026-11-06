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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Animation
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.Gradient
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Insights
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Timeline
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material.icons.filled.WaterDrop
import androidx.compose.material.icons.filled.Waves
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.MacroStore
import dev.geode.data.NoteResponseStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.BlendMode
import dev.geode.render.EnvBand
import dev.geode.render.EventRecipe
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
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
import dev.geode.render.scene.CustomizeTab
import dev.geode.render.scene.ParamFormat
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.ParamUnit
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneParams
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.UiSound
import kotlin.math.ln
import kotlin.math.pow
import kotlin.reflect.KProperty1

/**
 * Customize's deeper pages: each one opens from a row on its page and drills in, replacing the
 * list inside the same sheet, with ← and its title, the way Android Settings and the photo editors
 * do it (docs/ui-redesign). Back returns to the row it opened from.
 *
 * WHY a data-driven list: the next wave of the reactive work (a reaction grid, Moods, Phone motion,
 * Colour follow and the modulation matrix) each arrives as one more entry here plus one branch in
 * [SubPageBody]; the rows, the paywall, back, search and the ‹ › arrows need no change.
 * [MODULATION_MATRIX] is that slot, reserved and not offered yet.
 *
 * [page] is the Customize page whose row opens it. Advanced's three ([stepped]) step sideways with
 * ‹ › in their header, "2 of 3", because a user going through Advanced reads them as one set.
 * Every one but the macro sets is Pro, as the Advanced cards they came from were: a free user sees
 * the row with a ♛, and a tap opens the paywall instead of the page.
 */
internal enum class CustomizeSubPage(
    val title: String,
    val icon: ImageVector,
    val page: CustomizeTab,
    val pro: Boolean = true,
    /** False for a slot reserved in code that the panel does not offer yet. */
    val available: Boolean = true,
) {
    SETS("Sets & macros", Icons.Filled.Tune, CustomizeTab.MACROS, pro = false),
    PALETTE("Make a palette", Icons.Filled.Gradient, CustomizeTab.LOOK),
    CAMERA("Camera", Icons.Filled.Videocam, CustomizeTab.MOTION),
    LIQUID("Liquid", Icons.Filled.Waves, CustomizeTab.MOTION),
    RIPPLES("Water ripples", Icons.Filled.WaterDrop, CustomizeTab.MOTION),
    LISTEN("How it listens", Icons.Filled.GraphicEq, CustomizeTab.MUSIC),
    NOTE_KEY("Notes & key", Icons.Filled.MusicNote, CustomizeTab.MUSIC),
    REACTIONS("Reactions", Icons.Filled.Bolt, CustomizeTab.MODULATE),
    ENVELOPES("Envelopes", Icons.Filled.Insights, CustomizeTab.MODULATE),
    WAVES("Waves", Icons.Filled.Timeline, CustomizeTab.MODULATE),
    LAYERS("Layers", Icons.Filled.Layers, CustomizeTab.ADVANCED),
    TRANSITIONS("Transitions", Icons.Filled.Animation, CustomizeTab.ADVANCED),
    SHADER_CODE("Shader code", Icons.Filled.Code, CustomizeTab.ADVANCED),

    /** Reserved for the modulation matrix (any sound or wave to any control). Not built yet. */
    MODULATION_MATRIX("Modulation matrix", Icons.Filled.Bolt, CustomizeTab.MODULATE, available = false),
    ;

    /** Advanced's groups, stepped with ‹ › rather than one back and forth through the list. */
    val stepped: Boolean get() = page == CustomizeTab.ADVANCED

    /** Whether the style on screen has anything for this page. */
    fun appliesTo(sceneId: String): Boolean =
        available &&
            when (this) {
                SHADER_CODE -> SceneCapabilities.hasShaderLook(sceneId) || SceneCapabilities.isFluid(sceneId)
                else -> true
            }

    companion object {
        /** The deeper pages [page] opens on [sceneId], in order. */
        fun of(
            page: CustomizeTab,
            sceneId: String,
        ): List<CustomizeSubPage> = entries.filter { it.page == page && it.appliesTo(sceneId) }
    }
}

/** What a deeper page edits, gathered once so each page reads plain values. */
internal class AdvancedHost(
    val viewModel: PlayerViewModel,
    val visualsViewModel: VisualsViewModel,
    val visualizerView: VisualizerView,
)

/** A page's row that drills into one of its deeper pages: icon, name, ♛ when it is Pro, chevron. */
@Composable
internal fun SubPageRow(
    sub: CustomizeSubPage,
    locked: Boolean,
    onClick: () -> Unit,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 48.dp)
            .clickable(role = Role.Button, onClickLabel = if (locked) "Unlock with Pro" else "Open", onClick = onClick)
            .semantics(mergeDescendants = true) {}
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(sub.icon, contentDescription = null, tint = accentTextColor())
        Text(sub.title, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.padding(start = 16.dp).weight(1f))
        if (locked) ProCrown()
        Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/**
 * The ♛ that marks Pro on a page or a row: Customize's pages and cards and the Styles list draw this
 * one, so Pro reads the same everywhere. A real crown ([StoneIcon.CROWN]); the Material Icons
 * `WorkspacePremium` it replaced is a rosette, which device QA read as a ribbon badge.
 */
@Composable
internal fun ProCrown(modifier: Modifier = Modifier) {
    StoneIconArt(
        StoneIcon.CROWN,
        contentDescription = "Pro",
        modifier = modifier.padding(horizontal = 4.dp).size(18.dp),
        tint = accentTextColor(),
    )
}

/**
 * A deeper page drilled into: ← and its title, ‹ › "2 of 3" for Advanced's groups, then the page.
 * [siblings] are the groups the arrows step through; empty for a page that is not stepped.
 */
@Composable
internal fun SubPageScreen(
    sub: CustomizeSubPage,
    host: AdvancedHost,
    siblings: List<CustomizeSubPage>,
    onBack: () -> Unit,
    onStep: (CustomizeSubPage) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back to ${sub.page.title}")
            }
            val at = siblings.indexOf(sub)
            if (sub.stepped && at >= 0) {
                IconButton(onClick = { onStep(siblings[at - 1]) }, enabled = at > 0) {
                    Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, contentDescription = "Previous group")
                }
                Text(
                    sub.title,
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.weight(1f).semantics { heading() },
                )
                Text(
                    "${at + 1} of ${siblings.size}",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                IconButton(onClick = { onStep(siblings[at + 1]) }, enabled = at < siblings.lastIndex) {
                    Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = "Next group")
                }
            } else {
                Text(sub.title, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f).semantics { heading() })
            }
        }
        Column(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp),
        ) { SubPageBody(sub, host) }
    }
}

@Composable
internal fun SubPageBody(
    sub: CustomizeSubPage,
    host: AdvancedHost,
) {
    val viz by host.viewModel.vizState.collectAsStateWithLifecycle()
    val p = viz.params
    val onChange: (SceneParams) -> Unit = host.visualsViewModel::editSceneParams
    when (sub) {
        CustomizeSubPage.SETS -> {
            val context = LocalContext.current
            val store = remember { MacroStore.get(context) }
            val macros by store.state.collectAsStateWithLifecycle()
            val proLock = LocalProLock.current
            MacroSetsPage(macros, store, canSavePresets = proLock.unlocked, onLockedSave = proLock.onLocked)
        }
        CustomizeSubPage.LISTEN ->
            ListenPage(p, onChange, attack = viz.attack, decay = viz.decay, onReactivityChange = host.viewModel::setReactivity)
        CustomizeSubPage.ENVELOPES -> {
            val adsrs by host.visualsViewModel.adsrs.collectAsStateWithLifecycle()
            if (sectionVisible()) {
                for (i in 0 until AdsrEngine.COUNT) {
                    AdsrCard(index = i, config = adsrs.getOrElse(i) { AdsrConfig() }, onChange = { host.visualsViewModel.setAdsr(i, it) })
                }
            }
        }
        CustomizeSubPage.WAVES -> {
            val lfos by host.visualsViewModel.lfos.collectAsStateWithLifecycle()
            if (sectionVisible()) {
                for (i in 0 until LfoEngine.SLOTS) {
                    ModulatorCard(index = i, config = lfos.getOrElse(i) { LfoConfig() }, onChange = { host.visualsViewModel.setLfo(i, it) })
                }
            }
        }
        CustomizeSubPage.REACTIONS -> {
            val reactions by host.visualsViewModel.reactions.collectAsStateWithLifecycle()
            val handMask by host.visualsViewModel.handMask.collectAsStateWithLifecycle()
            ReactionsSection(p, reactions, host.visualsViewModel::setReactions, host.visualsViewModel::rerollReactions, handMask)
        }
        CustomizeSubPage.NOTE_KEY -> NoteKeyPage(p, onChange)
        CustomizeSubPage.LAYERS -> LayersPage()
        CustomizeSubPage.CAMERA -> CameraPage(p, onChange)
        CustomizeSubPage.TRANSITIONS ->
            TransitionsPage(
                p,
                onChange,
                transitionId = viz.transitionId,
                transitionDurationSec = viz.transitionDurationSec,
                onTransitionId = host.viewModel::setTransitionId,
                onTransitionDuration = host.viewModel::setTransitionDuration,
            )
        CustomizeSubPage.PALETTE -> if (sectionVisible()) PaletteMakerCard(p, onChange, rememberSavedPalettes())
        CustomizeSubPage.LIQUID -> {
            val liquidMode by host.visualsViewModel.liquidMode.collectAsStateWithLifecycle()
            LiquidPage(p, onChange, liquidMode = liquidMode)
        }
        CustomizeSubPage.RIPPLES -> RipplesPage(p, onChange)
        CustomizeSubPage.SHADER_CODE -> if (sectionVisible()) ShaderCodePage(host)
        CustomizeSubPage.MODULATION_MATRIX -> Unit
    }
}

/**
 * Music › How it listens: how strongly and how fast the picture follows the sound. Its envelopes
 * and waves are Modulate's ([CustomizeSubPage.ENVELOPES], [CustomizeSubPage.WAVES]).
 */
@Composable
internal fun ListenPage(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    attack: Float,
    decay: Float,
    onReactivityChange: (Float, Float) -> Unit,
) {
    Column {
        SectionHeader("How strongly it listens")
        ControlHint("Everything here reads the sound as it arrives, so the mic behaves exactly like a song.")
        LabeledSlider(
            ParamKeys.AUDIO_DRIVE,
            p,
            SceneParams::audioDrive,
            display = "Sound strength",
        ) {
            onChange(p.copy(audioDrive = it))
        }
        SectionHeader("Slow motion under the music")
        ControlHint("The picture breathes with the bass, drifts with the mids and leans its colour toward the song's key. Zero settles it.")
        LabeledSlider(ParamKeys.MOTION_AMOUNT, p, SceneParams::motionAmount) {
            onChange(p.copy(motionAmount = it))
        }
        LabeledSlider(ParamKeys.MOTION_BREATH, p, SceneParams::motionBreath) {
            onChange(p.copy(motionBreath = it))
        }
        LabeledSlider(ParamKeys.MOTION_DRIFT, p, SceneParams::motionDrift) {
            onChange(p.copy(motionDrift = it))
        }
        LabeledSlider(ParamKeys.MOTION_HUE, p, SceneParams::motionHue) {
            onChange(p.copy(motionHue = it))
        }
        SectionHeader("Bass, mids and highs", ParamScope.BAND_GAINS)
        LabeledSlider(ParamKeys.BASS_GAIN, p, SceneParams::bassGain, display = "Bass") {
            onChange(p.copy(bassGain = it))
        }
        LabeledSlider(ParamKeys.MID_GAIN, p, SceneParams::midGain, display = "Mids") {
            onChange(p.copy(midGain = it))
        }
        LabeledSlider(ParamKeys.TREBLE_GAIN, p, SceneParams::trebGain, display = "Highs") {
            onChange(p.copy(trebGain = it))
        }
        SectionHeader("Rise and fall", ParamScope.BAND_GAINS)
        ControlHint(
            "How fast the picture rises with a sound and falls after it. A short rise snaps, a long fall smears.",
            ParamScope.BAND_GAINS,
        )
        LabeledSlider(REACTIVITY_ATTACK, attack, 0.05f..1f, scope = ParamScope.BAND_GAINS) { onReactivityChange(it, decay) }
        LabeledSlider(REACTIVITY_DECAY, decay, 0.02f..0.6f, scope = ParamScope.BAND_GAINS) { onReactivityChange(attack, it) }
    }
}

/**
 * Advanced › Reactions: the route list.
 *
 * Shows the style's own reactions until the user edits one; the first edit copies them into the
 * user's own list, and "Use the style's own reactions" drops it again. Event recipes (Events) and
 * the reaction strength live on the simple Events page.
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
            "Each style comes with its own reactions: which sounds push the camera, change the colour, reshape, spin or " +
                "grow it. Shuffle for a new set; edit any one to make the list yours."
        } else {
            "Your own reactions, used on every style. \"Use the style's own reactions\" lets each style choose again."
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
        FilledTonalButton(onClick = { tap(onReroll) }) { Text("Shuffle reactions") }
        if (reactions != null) {
            FilledTonalButton(onClick = { tap { onReactionsChange(null) } }) { Text("Use the style's own reactions") }
        }
        if (routes.size < ReactionRoute.MAX_ROUTES) {
            FilledTonalButton(onClick = { tap { onReactionsChange(routes + NEW_REACTION) } }) { Text("Add") }
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
                TextButton(onClick = { picking = ReactionPick.SOURCE }) { Text("Sound: ${route.source.label}") }
                TextButton(onClick = { picking = ReactionPick.TARGET }) { Text("Moves: ${route.target.label}") }
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
            TextButton(onClick = onRemove) { Text("Remove") }
        }
    }
    picking?.let { which ->
        AlertDialog(
            onDismissRequest = { picking = null },
            title = { Text(if (which == ReactionPick.SOURCE) "Which sound" else "What it moves") },
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
            confirmButton = { TextButton(onClick = { picking = null }) { Text("Close") } },
        )
    }
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
            Text("Wave ${index + 1}", style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
            Switch(checked = config.enabled, onCheckedChange = { onChange(config.copy(enabled = it)) })
        }
        if (config.enabled) {
            TextButton(onClick = { showTargetPicker = true }) { Text("Moves: ${config.target.label}") }
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
                RateSecondsSlider(config.rateSeconds) { onChange(config.copy(rateSeconds = it)) }
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
            title = { Text("What wave ${index + 1} moves") },
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
            confirmButton = { TextButton(onClick = { showTargetPicker = false }) { Text("Close") } },
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
    Column(Modifier.padding(vertical = 2.dp)) {
        Text("One cycle · $shown", style = MaterialTheme.typography.bodyMedium)
        Slider(
            value = t,
            onValueChange = { onChange(lo * (hi / lo).pow(it)) },
            valueRange = 0f..1f,
            // A thumb at 0 sits in the back-gesture strip; without the exclusion a drag from it leaves the screen.
            modifier =
                Modifier.fillMaxWidth().systemGestureExclusion().semantics {
                    contentDescription = "One cycle"
                    stateDescription = shown
                },
        )
    }
}

@Composable
private fun AdsrCard(
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
        CardSubHeader("Shape")
        LabeledSlider(ENV_ATTACK, config.attack, 0.005f..1f, unit = ParamUnit.SECONDS) { onChange(config.copy(attack = it)) }
        LabeledSlider(ENV_DECAY, config.decay, 0.01f..1.5f, unit = ParamUnit.SECONDS) { onChange(config.copy(decay = it)) }
        LabeledSlider(ENV_SUSTAIN, config.sustain, 0f..1f) { onChange(config.copy(sustain = it)) }
        LabeledSlider(ENV_RELEASE, config.release, 0.02f..2f, unit = ParamUnit.SECONDS) { onChange(config.copy(release = it)) }
        LabeledSlider(ENV_AMOUNT, config.amount, 0f..1.5f, unit = ParamUnit.PERCENT) { onChange(config.copy(amount = it)) }
        CardSubHeader("Hold while loud")
        ControlHint(
            "The envelope holds while the chosen sound stays above the level and lets go when it drops, so the same hit " +
                "opens a long swell in a loud chorus and a short blip in a quiet verse.",
        )
        DropdownRow(ENV_GATE_SOURCE, EnvBand.entries.map { it.label }, EnvBand.entries.indexOf(config.band), lockKey = ENV_GATE_SOURCE) {
            onChange(config.copy(band = EnvBand.entries[it]))
        }
        LabeledSlider(ENV_GATE_LEVEL, config.gateThreshold, 0.05f..1f) { onChange(config.copy(gateThreshold = it)) }
        CheckRow(ENV_SUSTAIN_TRACK, config.sustainTrack) { onChange(config.copy(sustainTrack = it)) }
        CheckRow(ENV_RETRIGGER, config.retrigger) { onChange(config.copy(retrigger = it)) }
    }
}

/** Advanced › Layers: a second style drawn under the active one. */
@Composable
internal fun LayersPage() {
    val layers by LayersBus.state.collectAsStateWithLifecycle()
    val layerScenes by LayersBus.availableScenes.collectAsStateWithLifecycle()
    val activeScene by LayersBus.activeSceneId.collectAsStateWithLifecycle()
    ControlHint(
        "Draws a second style every frame and blends it under the active one. A whole extra scene, so it costs " +
            "smoothness. Screen only: a video export carries the active style alone.",
    )
    CheckRow(LAYERS_ENABLED, layers.enabled, scope = ParamScope.UNIVERSAL) { on ->
        val scene = layers.sceneId ?: layerScenes.firstOrNull { it != activeScene }
        LayersBus.state.value = layers.copy(enabled = on, sceneId = scene)
    }
    if (!layers.enabled || !sectionVisible()) return
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
 * Advanced › Note & key: how strongly each part of the musical layer (core/viz/NoteResponse) moves this
 * style, saved with the style like every Customize value. The switch that runs the layer at all is
 * Settings › Behaviour (free, on by default); with it off these sliders do nothing, so the page says so
 * rather than letting a user drag a dead control.
 */
@Composable
private fun NoteKeyPage(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val context = LocalContext.current
    val running by remember { NoteResponseStore.get(context) }.enabled.collectAsStateWithLifecycle()
    if (!running && sectionVisible()) ControlHint(stringResource(R.string.note_key_off))
    ControlHint(stringResource(R.string.note_key_intro))
    NoteKeySlider(R.string.note_key_color, R.string.note_key_color_hint, p, SceneParams::noteColor) {
        onChange(p.copy(noteColor = it))
    }
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

/** Advanced › Camera: the one rig every style, the wallpaper and exports share. */
@Composable
private fun CameraPage(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    ControlHint(
        "One camera for every style, the wallpaper and exports. 3D styles fly through their world; flat styles are " +
            "seen on a plane the camera pans, tilts and pushes toward. Push, turn, tilt and roll are where the " +
            "reactions start from.",
    )
    ParamChips(
        ParamKeys.CAM_PATH,
        SceneParams.CAMERA_PATHS,
        p.camPath.coerceIn(0, SceneParams.CAMERA_PATHS.lastIndex),
        ParamScope.UNIVERSAL,
    ) {
        onChange(p.copy(camPath = it))
    }
    LabeledSlider(ParamKeys.CAM_FLY, p, SceneParams::camFly) { onChange(p.copy(camFly = it)) }
    LabeledSlider(ParamKeys.CAM_ORBIT, p, SceneParams::camOrbit) { onChange(p.copy(camOrbit = it)) }
    LabeledSlider(ParamKeys.CAM_FOV, p, SceneParams::camFov) { onChange(p.copy(camFov = it)) }
    LabeledSlider(ParamKeys.CAM_PUSH, p, SceneParams::camPush) { onChange(p.copy(camPush = it)) }
    LabeledSlider(ParamKeys.CAM_YAW, p, SceneParams::camYaw) { onChange(p.copy(camYaw = it)) }
    LabeledSlider(ParamKeys.CAM_PITCH, p, SceneParams::camPitch) { onChange(p.copy(camPitch = it)) }
    LabeledSlider(ParamKeys.CAM_ROLL, p, SceneParams::camRoll) { onChange(p.copy(camRoll = it)) }
    LabeledSlider(ParamKeys.CAM_SHAKE, p, SceneParams::camShake) { onChange(p.copy(camShake = it)) }
    LabeledSlider(ParamKeys.CAM_SMOOTH, p, SceneParams::camSmooth) { onChange(p.copy(camSmooth = it)) }
    CheckRow(ParamKeys.CAM_TOUCH, p.camTouch) { onChange(p.copy(camTouch = it)) }
    CheckRow(ParamKeys.CAM_GYRO, p.camGyro) { onChange(p.copy(camGyro = it)) }
    ControlHint("Exports always fly the recorded path; the phone's tilt is never baked in.")
}

private const val TRANSITION_CHIP_LIMIT = 40

/** Advanced › Transitions: how one style hands over to the next, and how changes glide in. */
@Composable
private fun TransitionsPage(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    transitionId: String?,
    transitionDurationSec: Float,
    onTransitionId: (String) -> Unit,
    onTransitionDuration: (Float) -> Unit,
) {
    if (transitionId != null && sectionVisible()) {
        SectionHeader("Style transition")
        val ctx = LocalContext.current
        val library = remember { TransitionCatalog.library(ctx) }
        // "cut" is left out: VisualSafety always plays it as a fade, so its entry lied.
        val builtIn = remember { TransitionCatalog.BUILT_IN_IDS.filter { it != "cut" } }
        DropdownRow(
            stringResource(R.string.transition_built_in),
            builtIn,
            builtIn.indexOf(transitionId),
            placeholder = stringResource(R.string.transition_from_library),
        ) { onTransitionId(builtIn[it]) }
        if (library.isNotEmpty()) {
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
                label = { Text("Search transitions") },
            )
            val shown =
                remember(query, library) {
                    if (query.isBlank()) library else library.filter { it.name.contains(query, ignoreCase = true) }
                }
            val names = shown.take(TRANSITION_CHIP_LIMIT).map { it.name }
            DropdownRow(
                stringResource(R.string.transition_library),
                names,
                names.indexOf(transitionId),
                placeholder = stringResource(R.string.transition_library_pick, shown.size),
            ) { onTransitionId(names[it]) }
            if (shown.size > TRANSITION_CHIP_LIMIT) {
                ControlHint("${shown.size - TRANSITION_CHIP_LIMIT} more - narrow the search to reach them.")
            }
        }
        LabeledSlider(TRANSITION_LENGTH, transitionDurationSec, 0.3f..5f, scope = ParamScope.UNIVERSAL, unit = ParamUnit.SECONDS) {
            onTransitionDuration(it)
        }
    }
    SectionHeader("Glide between settings")
    ControlHint("Slider changes and preset loads glide to their new values over this time.")
    LabeledSlider(PARAM_FADE, p, SceneParams::paramFadeSec, unit = ParamUnit.SECONDS) {
        onChange(p.copy(paramFadeSec = it))
    }
}

/**
 * Advanced › Liquid: the switch that turns every style into a flowing liquid, and how it looks.
 *
 * [liquidMode] is the renderer's live state (0 off, 1 full, 2 lite, 3 warp, 4 paused); 2 to 4 only
 * happen when the phone runs hot, and the note says so rather than leaving the picture to look broken.
 */
@Composable
internal fun LiquidPage(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    liquidMode: Int,
) {
    SectionHeader("Liquid")
    ControlHint("Every style turns liquid: touch and sound mold it, and the camera dives forever.")
    CheckRow(FLOWFIELD_ENABLED, p.flowEnabled, display = "Liquid mode", scope = ParamScope.UNIVERSAL) {
        onChange(p.copy(flowEnabled = it))
    }
    liquidHotNote(liquidMode)?.let { note ->
        Text(
            note,
            modifier = Modifier.padding(top = 4.dp, bottom = 2.dp),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.error,
        )
    }
    if (p.flowEnabled) {
        LabeledSlider(
            ParamKeys.FLOW_STRENGTH,
            p,
            SceneParams::flowStrength,
            display = "Liquidity",
            scope = ParamScope.UNIVERSAL,
        ) {
            onChange(p.copy(flowStrength = it))
        }
        LabeledSlider(ParamKeys.FLOW_FORCE, p, SceneParams::flowForce, display = "Push", scope = ParamScope.UNIVERSAL) {
            onChange(p.copy(flowForce = it))
        }
        LabeledSlider(ParamKeys.FLOW_CURL, p, SceneParams::flowCurl, display = "Swirl", scope = ParamScope.UNIVERSAL) {
            onChange(p.copy(flowCurl = it))
        }
        LabeledSlider(ParamKeys.DIVE_SPEED, p, SceneParams::endlessZoomSpeed, scope = ParamScope.UNIVERSAL) {
            onChange(p.copy(endlessZoomSpeed = it))
        }
        ParamChips(ParamKeys.LIQUID_BORN, LIQUID_BORN_OPTIONS, p.liquidBorn.coerceIn(0, 3), scope = ParamScope.UNIVERSAL) {
            onChange(p.copy(liquidBorn = it))
        }
        ParamChips(ParamKeys.LIQUID_AGE, LIQUID_AGE_OPTIONS, p.liquidAge.coerceIn(0, 2), scope = ParamScope.UNIVERSAL) {
            onChange(p.copy(liquidAge = it))
        }
        ParamChips(ParamKeys.LIQUID_SURFACE, LIQUID_SURFACE_OPTIONS, p.liquidSurface.coerceIn(0, 2), scope = ParamScope.UNIVERSAL) {
            onChange(p.copy(liquidSurface = it))
        }
    }
}

private val LIQUID_BORN_OPTIONS = listOf("Live style", "Style re-rolled", "Next style", "Forms")
private val LIQUID_AGE_OPTIONS = listOf("Pure palette", "Fade deep", "Ink bleed")
private val LIQUID_SURFACE_OPTIONS = listOf("Ink", "Gloss", "Glass")

/** The hot-phone line for the renderer's liquid mode (0 off, 1 full, 2 lite, 3 warp, 4 paused), or null. */
internal fun liquidHotNote(liquidMode: Int): String? =
    when (liquidMode) {
        2, 3 -> "Liquid simplified: phone is hot"
        4 -> "Liquid paused: phone is hot"
        else -> null
    }

/** Advanced › Water ripples: water on top of any style. */
@Composable
private fun RipplesPage(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val isWaterScene = ParamScope.WATER.appliesTo(LocalSceneId.current)
    SectionHeader("Water ripples", ParamScope.RIPPLE_OVERLAY)
    ControlHint(
        "Water on top of any style: hits drop rings that bend the picture, the highs sprinkle small drops, and glint " +
            "adds a sparkle on the crests.",
        ParamScope.RIPPLE_OVERLAY,
    )
    CheckRow(RIPPLES_ENABLED, p.rippleOverlayEnabled, scope = ParamScope.RIPPLE_OVERLAY) { onChange(p.copy(rippleOverlayEnabled = it)) }
    if (p.rippleOverlayEnabled && !isWaterScene) {
        LabeledSlider(ParamKeys.WAVE_SPEED, p, SceneParams::waterWaveSpeed) {
            onChange(p.copy(waterWaveSpeed = it))
        }
        LabeledSlider(ParamKeys.DAMPING, p, SceneParams::waterDamping, display = "Calming") {
            onChange(p.copy(waterDamping = it))
        }
        LabeledSlider(
            ParamKeys.RIPPLE_OVERLAY_STRENGTH,
            p,
            SceneParams::rippleOverlayStrength,
            display = "Ripple strength",
        ) { onChange(p.copy(rippleOverlayStrength = it)) }
        LabeledSlider(ParamKeys.RIPPLE_GLINT, p, SceneParams::rippleOverlaySpecular) {
            onChange(p.copy(rippleOverlaySpecular = it))
        }
    }
}

private const val MAX_SAVED_SHADER_DRAFT_CHARS = 8 * 1024

private val ShaderDraftSaver =
    Saver<String, String>(
        save = { draft -> draft.takeIf { it.length <= MAX_SAVED_SHADER_DRAFT_CHARS } },
        restore = { it },
    )

/** Advanced › Shader code: the style's own fragment shader, and the custom fluid shaders on Fluid. */
@Composable
private fun ShaderCodePage(host: AdvancedHost) {
    val viz by host.viewModel.vizState.collectAsStateWithLifecycle()
    if (SceneCapabilities.hasShaderLook(viz.sceneId)) {
        var source by rememberSaveable(viz.sceneId, stateSaver = ShaderDraftSaver) {
            mutableStateOf(host.visualizerView.visualizerRenderer.customShaderFor(viz.sceneId) ?: "")
        }
        SectionHeader("This style's shader")
        ControlHint(
            "The fragment shader for this style: view(), pal(), grade() and the sound uniforms are available. With the " +
                "flow field on, declare `uniform sampler2D uFlow` and `uniform float uFlowStrength` to bend by it.",
        )
        OutlinedTextField(
            value = source,
            onValueChange = { source = it },
            modifier = Modifier.fillMaxWidth().height(360.dp),
            textStyle = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
        )
        viz.shaderError?.let { Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall) }
        Row(horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
            val sounds = LocalUiSounds.current
            Button(onClick = {
                sounds.play(UiSound.CONFIRM)
                host.viewModel.applyCustomShader(source)
            }) { Text("Apply shader") }
            TextButton(onClick = { source = host.visualizerView.visualizerRenderer.customShaderFor(viz.sceneId) ?: "" }) { Text("Revert") }
        }
    }
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
    SectionHeader("Custom fluid shaders")
    ControlHint(
        "Replace how the fluid is pushed (force) and coloured (dye). Both start from the built-in splat (uMode 0 = " +
            "push, 1 = colour); a failed compile keeps the last good one. Extra uniforms: uDt, uDx, uTime, uBass, " +
            "uMid, uTreble, uEnergy, uBeat. Clear the text to go back to the built-in.",
    )
    val ctx = LocalContext.current
    val template = remember { FluidShaderTemplate.splat(ctx) }
    var forceSrc by remember { mutableStateOf(template) }
    var dyeSrc by remember { mutableStateOf(template) }
    var editorsUsed by remember { mutableStateOf(false) }
    Text("Force shader", style = MaterialTheme.typography.bodyMedium)
    OutlinedTextField(
        value = forceSrc,
        onValueChange = {
            forceSrc = it
            editorsUsed = true
        },
        modifier = Modifier.fillMaxWidth().height(180.dp),
        textStyle = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
    )
    Text("Dye shader", style = MaterialTheme.typography.bodyMedium)
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
            val f = forceSrc.takeIf { editorsUsed && it.isNotBlank() && it != template }
            val d = dyeSrc.takeIf { editorsUsed && it.isNotBlank() && it != template }
            onApplyInjectionShaders(f, d)
        }) { Text("Apply shaders") }
        TextButton(onClick = {
            forceSrc = template
            dyeSrc = template
            onApplyInjectionShaders(null, null)
        }) { Text("Back to built-in") }
    }
}

// Names for the controls that are panel machinery rather than scene parameters. They live in one
// place so the search matches them and nothing has to spell a string twice.
private const val REACTIVITY_ATTACK = "Rise"
private const val REACTIVITY_DECAY = "Fall"
private const val PARAM_FADE = "Glide time"
private const val TRANSITION_LENGTH = "Transition length"
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
private const val FLOWFIELD_ENABLED = "Flow field on"
private const val RIPPLES_ENABLED = "Water ripples on"
private const val LAYERS_ENABLED = "Layers enabled"
private const val LAYER_MIX = "Layer mix"
