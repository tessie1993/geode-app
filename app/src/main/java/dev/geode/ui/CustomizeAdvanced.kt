package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Animation
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.Gradient
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material.icons.filled.Waves
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.AssistChip
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
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
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
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
import dev.geode.render.ReactMode
import dev.geode.render.ReactSource
import dev.geode.render.ReactSourceGroup
import dev.geode.render.ReactionRoute
import dev.geode.render.TransitionCatalog
import dev.geode.render.VisualizerView
import dev.geode.render.fluid.FluidShaderTemplate
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.ParamFormat
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.ParamUnit
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneParams
import kotlin.math.ln
import kotlin.math.pow

/**
 * Customize › Advanced: one card per deep editor, each opening its own page inside the panel.
 *
 * WHY a data-driven list: the next wave of the reactive work (Camera moves, a reaction grid,
 * Moods, Phone motion, Colour follow and the modulation matrix, docs/research/REACTIVE_VISUALS.md
 * §5 and §7) each arrives as a new card. Adding one is one entry here plus one branch in
 * [AdvancedSectionBody]; the list, the paywall lock, back, search and the sticky toolbar need no
 * change. [MODULATION_MATRIX] is that slot, reserved and not offered yet.
 *
 * The whole of Advanced is Pro: a free user sees every card with a lock, and a tap opens the
 * paywall instead of the page.
 */
internal enum class AdvancedSection(
    val title: String,
    val summary: String,
    val icon: ImageVector,
    /** False for a slot reserved in code that the panel does not offer yet. */
    val available: Boolean = true,
) {
    SOUND_RESPONSE(
        "Sound response",
        "How fast the picture follows the music, the bass-mids-highs balance, waves and envelopes.",
        Icons.Filled.GraphicEq,
    ),
    REACTIONS("Reactions", "Every sound-to-movement link, one at a time.", Icons.Filled.Bolt),
    LAYERS("Layers", "Blend a second style under this one.", Icons.Filled.Layers),
    CAMERA("Camera", "The flight path, field of view, shake and touch or tilt to look.", Icons.Filled.Videocam),
    TRANSITIONS("Transitions", "How styles hand over, and how changes glide in.", Icons.Filled.Animation),
    GRADIENT("Gradient maker", "Build and save your own palettes.", Icons.Filled.Gradient),
    FLOW("Flow field & water ripples", "Liquid motion and ripples that ride on every style.", Icons.Filled.Waves),
    SHADER_CODE("Shader code", "Edit the style's shader, or write custom fluid shaders.", Icons.Filled.Code),

    /** Reserved for the modulation matrix (any sound or wave to any control). Not built yet. */
    MODULATION_MATRIX("Modulation matrix", "Connect any sound or wave to any control.", Icons.Filled.Bolt, available = false),
    ;

    /** Whether the style on screen has anything for this page. */
    fun appliesTo(sceneId: String): Boolean =
        available &&
            when (this) {
                SHADER_CODE -> SceneCapabilities.hasShaderLook(sceneId) || SceneCapabilities.isFluid(sceneId)
                else -> true
            }
}

/** What an Advanced page edits, gathered once so each page reads plain values. */
internal class AdvancedHost(
    val viewModel: PlayerViewModel,
    val visualsViewModel: VisualsViewModel,
    val visualizerView: VisualizerView,
)

@Composable
internal fun AdvancedTab(
    host: AdvancedHost,
    unlocked: Boolean,
    onLocked: () -> Unit,
) {
    val viz by host.viewModel.vizState.collectAsStateWithLifecycle()
    val sections = AdvancedSection.entries.filter { it.appliesTo(viz.sceneId) }
    if (LocalParamQuery.current.isNotBlank()) {
        // Search reaches inside every page; a free user sees the matches locked.
        sections.forEach { section ->
            ProGated("advanced:${section.name}", section.title) { AdvancedSectionBody(section, host) }
        }
        return
    }
    var openName by rememberSaveable { mutableStateOf<String?>(null) }
    val open = sections.firstOrNull { it.name == openName }?.takeIf { unlocked }
    if (open != null) {
        BackHandler { openName = null }
        Column {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().padding(top = 4.dp)) {
                IconButton(onClick = { openName = null }) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back to Advanced")
                }
                Text(open.title, style = MaterialTheme.typography.titleMedium, modifier = Modifier.semantics { heading() })
            }
            AdvancedSectionBody(open, host)
        }
        return
    }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 8.dp)) {
        if (!unlocked) AdvancedUpsell(onLocked)
        sections.forEach { section ->
            AdvancedCard(section, locked = !unlocked) { if (unlocked) openName = section.name else onLocked() }
        }
    }
}

@Composable
private fun AdvancedUpsell(onUnlock: () -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f), RoundedCornerShape(16.dp))
            .padding(16.dp),
    ) {
        Text("Advanced is part of Pro", style = MaterialTheme.typography.titleSmall, modifier = Modifier.semantics { heading() })
        Text(
            "Fine-tune how every sound moves the picture, layer two styles, fly the camera and write your own shaders.",
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(top = 4.dp, bottom = 8.dp),
        )
        CrystalButton(onClick = onUnlock) { Text("Unlock") }
    }
}

@Composable
private fun AdvancedCard(
    section: AdvancedSection,
    locked: Boolean,
    onClick: () -> Unit,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 64.dp)
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f), RoundedCornerShape(12.dp))
            .clickable(role = Role.Button, onClickLabel = if (locked) "Unlock with Pro" else "Open", onClick = onClick)
            .semantics(mergeDescendants = true) {}
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(section.icon, contentDescription = null, tint = accentTextColor())
        Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
            Text(section.title, style = MaterialTheme.typography.titleSmall)
            Text(section.summary, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        if (locked) {
            Icon(Icons.Filled.Lock, contentDescription = "Pro")
        } else {
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null)
        }
    }
}

@Composable
private fun AdvancedSectionBody(
    section: AdvancedSection,
    host: AdvancedHost,
) {
    val viz by host.viewModel.vizState.collectAsStateWithLifecycle()
    val p = viz.params
    val onChange: (SceneParams) -> Unit = host.visualsViewModel::editSceneParams
    when (section) {
        AdvancedSection.SOUND_RESPONSE -> {
            val lfos by host.visualsViewModel.lfos.collectAsStateWithLifecycle()
            val adsrs by host.visualsViewModel.adsrs.collectAsStateWithLifecycle()
            SoundResponsePage(
                p,
                onChange,
                attack = viz.attack,
                decay = viz.decay,
                onReactivityChange = host.viewModel::setReactivity,
                lfos = lfos,
                onLfoChange = host.visualsViewModel::setLfo,
                adsr = adsrs,
                onAdsrChange = host.visualsViewModel::setAdsr,
            )
        }
        AdvancedSection.REACTIONS -> {
            val reactions by host.visualsViewModel.reactions.collectAsStateWithLifecycle()
            ReactionsSection(p, reactions, host.visualsViewModel::setReactions, host.visualsViewModel::rerollReactions)
        }
        AdvancedSection.LAYERS -> LayersPage()
        AdvancedSection.CAMERA -> CameraPage(p, onChange)
        AdvancedSection.TRANSITIONS ->
            TransitionsPage(
                p,
                onChange,
                transitionId = viz.transitionId,
                transitionDurationSec = viz.transitionDurationSec,
                onTransitionId = host.viewModel::setTransitionId,
                onTransitionDuration = host.viewModel::setTransitionDuration,
            )
        AdvancedSection.GRADIENT -> if (sectionVisible()) PaletteMakerCard(p, onChange, rememberSavedPalettes())
        AdvancedSection.FLOW -> FlowPage(p, onChange)
        AdvancedSection.SHADER_CODE -> if (sectionVisible()) ShaderCodePage(host)
        AdvancedSection.MODULATION_MATRIX -> Unit
    }
}

/**
 * Advanced › Sound response: how the picture listens. The waves (LFOs) can run steadily or follow
 * a sound (a band, the level, note starts), so they stay tied to the music's envelope rather than
 * ticking on their own; tempo-synced waves arrive with the modulation matrix.
 */
@Composable
internal fun SoundResponsePage(
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
        SectionHeader("How strongly it listens")
        ControlHint("Everything here reads the sound as it arrives, so the mic behaves exactly like a song.")
        LabeledSlider(
            ParamKeys.AUDIO_DRIVE,
            p.audioDrive,
            0.2f..2.5f,
            display = "Sound strength",
            default = SceneParams.DEFAULT.audioDrive,
        ) {
            onChange(p.copy(audioDrive = it))
        }
        SectionHeader("Slow motion under the music")
        ControlHint("The picture breathes with the bass, drifts with the mids and leans its colour toward the song's key. Zero settles it.")
        LabeledSlider(ParamKeys.MOTION_AMOUNT, p.motionAmount, 0f..1f, default = SceneParams.DEFAULT.motionAmount) {
            onChange(p.copy(motionAmount = it))
        }
        LabeledSlider(ParamKeys.MOTION_BREATH, p.motionBreath, 0f..1f, default = SceneParams.DEFAULT.motionBreath) {
            onChange(p.copy(motionBreath = it))
        }
        LabeledSlider(ParamKeys.MOTION_DRIFT, p.motionDrift, 0f..1f, default = SceneParams.DEFAULT.motionDrift) {
            onChange(p.copy(motionDrift = it))
        }
        LabeledSlider(ParamKeys.MOTION_HUE, p.motionHue, 0f..1f, default = SceneParams.DEFAULT.motionHue) {
            onChange(p.copy(motionHue = it))
        }
        SectionHeader("Bass, mids and highs", ParamScope.BAND_GAINS)
        LabeledSlider(ParamKeys.BASS_GAIN, p.bassGain, 0f..2f, display = "Bass", default = SceneParams.DEFAULT.bassGain) {
            onChange(p.copy(bassGain = it))
        }
        LabeledSlider(ParamKeys.MID_GAIN, p.midGain, 0f..2f, display = "Mids", default = SceneParams.DEFAULT.midGain) {
            onChange(p.copy(midGain = it))
        }
        LabeledSlider(ParamKeys.TREBLE_GAIN, p.trebGain, 0f..2f, display = "Highs", default = SceneParams.DEFAULT.trebGain) {
            onChange(p.copy(trebGain = it))
        }
        SectionHeader("Rise and fall", ParamScope.BAND_GAINS)
        ControlHint(
            "How fast the picture rises with a sound and falls after it. A short rise snaps, a long fall smears.",
            ParamScope.BAND_GAINS,
        )
        LabeledSlider(REACTIVITY_ATTACK, attack, 0.05f..1f, scope = ParamScope.BAND_GAINS) { onReactivityChange(it, decay) }
        LabeledSlider(REACTIVITY_DECAY, decay, 0.02f..0.6f, scope = ParamScope.BAND_GAINS) { onReactivityChange(attack, it) }
        SectionHeader("Envelopes on hits")
        ControlHint(
            "Two envelopes fired by the hits you hear. Each can move several controls at once: tap + to add one, tap a chip to remove it.",
        )
        if (sectionVisible()) {
            for (i in 0 until AdsrEngine.COUNT) {
                AdsrCard(index = i, config = adsr.getOrElse(i) { AdsrConfig() }, onChange = { onAdsrChange(i, it) })
            }
        }
        SectionHeader("Waves")
        ControlHint(
            "Each wave moves one control. Let it run steadily, or let it follow a sound (bass, level, note starts) so it " +
                "rises and falls with the music. Speed is the time for one cycle.",
        )
        if (sectionVisible()) {
            for (i in 0 until LfoEngine.SLOTS) {
                ModulatorCard(index = i, config = lfos.getOrElse(i) { LfoConfig() }, onChange = { onLfoChange(i, it) })
            }
        }
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
    val dna = remember(sceneId, p.reactSeed) { BuildStyleReactionsUseCase()(sceneId, p.reactSeed) }
    val routes = reactions ?: dna
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        CrystalButton(compact = true, filled = false, onClick = onReroll) { Text("Shuffle reactions") }
        if (reactions != null) {
            CrystalButton(compact = true, filled = false, onClick = { onReactionsChange(null) }) { Text("Use the style's own reactions") }
        }
        if (routes.size < ReactionRoute.MAX_ROUTES) {
            CrystalButton(compact = true, filled = false, onClick = { onReactionsChange(routes + NEW_REACTION) }) { Text("Add") }
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
        if (route.target != LfoTarget.NONE && !route.target.scope.appliesTo(sceneId)) {
            ControlHint("${route.target.label} does nothing on this style.")
        }
        if (expanded) {
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                TextButton(onClick = { picking = ReactionPick.SOURCE }) { Text("Sound: ${route.source.label}") }
                TextButton(onClick = { picking = ReactionPick.TARGET }) { Text("Moves: ${route.target.label}") }
            }
            ChipRow(ReactMode.entries.map { it.label }, ReactMode.entries.indexOf(route.mode)) {
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
            ChipRow(ModCurve.entries.map { it.label }, ModCurve.entries.indexOf(route.curve)) {
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
                            .filter { it != LfoTarget.NONE && it.chain == null && it.scope.appliesTo(sceneId) }
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
            if (config.target != LfoTarget.NONE && !config.target.scope.appliesTo(sceneId)) {
                ControlHint("${config.target.label} does nothing on this style - pick another.")
            }
            Text("Follows", style = MaterialTheme.typography.bodyMedium)
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
            Text("Direction", style = MaterialTheme.typography.bodyMedium)
            ChipRow(ModPolarity.entries.map { it.label }, ModPolarity.entries.indexOf(config.polarity)) {
                onChange(config.copy(polarity = ModPolarity.entries[it]))
            }
            Text("Curve", style = MaterialTheme.typography.bodyMedium)
            ChipRow(ModCurve.entries.map { it.label }, ModCurve.entries.indexOf(config.curve)) {
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
                        // A chain target may only point at a LATER wave, and a scene-parameter
                        // target is only offered where it can actually move the picture.
                        val chain = t.chain
                        val offered = if (chain != null) index < chain.slot else t.scope.appliesTo(sceneId)
                        if (offered) {
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
    val lo = LfoConfig.MIN_RATE_SECONDS
    val hi = LfoConfig.MAX_RATE_SECONDS
    val t = (ln(seconds.coerceIn(lo, hi) / lo) / ln(hi / lo)).coerceIn(0f, 1f)
    val shown = ParamFormat.format(seconds, ParamUnit.SECONDS)
    Column(Modifier.padding(vertical = 2.dp)) {
        Text("One cycle · $shown", style = MaterialTheme.typography.bodyMedium)
        CrystalSlider(
            value = t,
            onValueChange = { onChange(lo * (hi / lo).pow(it)) },
            valueRange = 0f..1f,
            modifier =
                Modifier.fillMaxWidth().semantics {
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
    var showAdd by remember { mutableStateOf(false) }
    val sceneId = LocalSceneId.current
    Column(modifier = Modifier.padding(vertical = 4.dp)) {
        CheckRow("Envelope ${index + 1} on", config.enabled) { onChange(config.copy(enabled = it)) }
        if (!config.enabled) return@Column
        Text("Moves:", style = MaterialTheme.typography.bodyMedium)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            config.targets.forEach { t ->
                AssistChip(
                    onClick = { onChange(config.copy(targets = config.targets - t)) },
                    label = { Text(t.label, style = MaterialTheme.typography.labelLarge) },
                    modifier = Modifier.semantics { contentDescription = "Remove ${t.label}" },
                )
            }
            Box {
                AssistChip(
                    onClick = { showAdd = true },
                    label = { Text("+", style = MaterialTheme.typography.labelLarge) },
                    modifier = Modifier.semantics { contentDescription = "Add a control" },
                )
                DropdownMenu(expanded = showAdd, onDismissRequest = { showAdd = false }) {
                    LfoTarget.entries
                        .filter { it != LfoTarget.NONE && it !in config.targets && (it.chain != null || it.scope.appliesTo(sceneId)) }
                        .forEach { t ->
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
        LockableChipLabel(ENV_GATE_SOURCE)
        ChipRow(EnvBand.entries.map { it.label }, EnvBand.entries.indexOf(config.band)) {
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
    Text("Blend", style = MaterialTheme.typography.bodyMedium)
    val blendNames = BlendMode.entries.map { mode -> mode.name.lowercase().replaceFirstChar { it.uppercase() } }
    ChipRow(blendNames, BlendMode.entries.indexOf(layers.blend)) {
        LayersBus.state.value = layers.copy(blend = BlendMode.entries[it])
    }
    LabeledSlider(LAYER_MIX, layers.mix, 0f..1f, scope = ParamScope.UNIVERSAL) { LayersBus.state.value = layers.copy(mix = it) }
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
    LabeledSlider(ParamKeys.CAM_FLY, p.camFly, 0f..2f, default = SceneParams.DEFAULT.camFly) { onChange(p.copy(camFly = it)) }
    LabeledSlider(ParamKeys.CAM_ORBIT, p.camOrbit, 0f..1f, default = SceneParams.DEFAULT.camOrbit) { onChange(p.copy(camOrbit = it)) }
    LabeledSlider(ParamKeys.CAM_FOV, p.camFov, 0.5f..2f, default = SceneParams.DEFAULT.camFov) { onChange(p.copy(camFov = it)) }
    LabeledSlider(ParamKeys.CAM_PUSH, p.camPush, -1f..1f, default = SceneParams.DEFAULT.camPush) { onChange(p.copy(camPush = it)) }
    LabeledSlider(ParamKeys.CAM_YAW, p.camYaw, -1f..1f, default = SceneParams.DEFAULT.camYaw) { onChange(p.copy(camYaw = it)) }
    LabeledSlider(ParamKeys.CAM_PITCH, p.camPitch, -1f..1f, default = SceneParams.DEFAULT.camPitch) { onChange(p.copy(camPitch = it)) }
    LabeledSlider(ParamKeys.CAM_ROLL, p.camRoll, -1f..1f, default = SceneParams.DEFAULT.camRoll) { onChange(p.copy(camRoll = it)) }
    LabeledSlider(ParamKeys.CAM_SHAKE, p.camShake, 0f..1f, default = SceneParams.DEFAULT.camShake) { onChange(p.copy(camShake = it)) }
    LabeledSlider(ParamKeys.CAM_SMOOTH, p.camSmooth, 0f..1f, default = SceneParams.DEFAULT.camSmooth) { onChange(p.copy(camSmooth = it)) }
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
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            // "cut" is left out: VisualSafety always plays it as a fade, so its chip lied.
            TransitionCatalog.BUILT_IN_IDS.filter { it != "cut" }.forEach { id ->
                FilterChip(selected = transitionId == id, onClick = { onTransitionId(id) }, label = { Text(id) })
            }
        }
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
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                shown.take(TRANSITION_CHIP_LIMIT).forEach { def ->
                    FilterChip(
                        selected = transitionId == def.name,
                        onClick = { onTransitionId(def.name) },
                        label = { Text(def.name, style = MaterialTheme.typography.labelLarge) },
                    )
                }
            }
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
    LabeledSlider(PARAM_FADE, p.paramFadeSec, 0f..5f, default = SceneParams.DEFAULT.paramFadeSec, unit = ParamUnit.SECONDS) {
        onChange(p.copy(paramFadeSec = it))
    }
}

/** Advanced › Flow field & water ripples: two effects that ride on any style. */
@Composable
private fun FlowPage(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val isWaterScene = ParamScope.WATER.appliesTo(LocalSceneId.current)
    SectionHeader("Flow field")
    ControlHint(
        "A shared liquid motion that bends any style: it distorts the finished picture (and exports), particle " +
            "styles can ride it, and shader code can read it.",
    )
    CheckRow(FLOWFIELD_ENABLED, p.flowEnabled, scope = ParamScope.UNIVERSAL) { onChange(p.copy(flowEnabled = it)) }
    if (p.flowEnabled) {
        LabeledSlider(ParamKeys.FLOW_STRENGTH, p.flowStrength, 0f..1f, default = SceneParams.DEFAULT.flowStrength) {
            onChange(p.copy(flowStrength = it))
        }
        LabeledSlider(ParamKeys.FLOW_FORCE, p.flowForce, 0f..3f, default = SceneParams.DEFAULT.flowForce) {
            onChange(p.copy(flowForce = it))
        }
        LabeledSlider(ParamKeys.FLOW_CURL, p.flowCurl, 0f..50f, display = "Flow swirl", default = SceneParams.DEFAULT.flowCurl) {
            onChange(p.copy(flowCurl = it))
        }
    }
    SectionHeader("Water ripples", ParamScope.RIPPLE_OVERLAY)
    ControlHint(
        "Water on top of any style: hits drop rings that bend the picture, the highs sprinkle small drops, and glint " +
            "adds a sparkle on the crests.",
        ParamScope.RIPPLE_OVERLAY,
    )
    CheckRow(RIPPLES_ENABLED, p.rippleOverlayEnabled, scope = ParamScope.RIPPLE_OVERLAY) { onChange(p.copy(rippleOverlayEnabled = it)) }
    if (p.rippleOverlayEnabled && !isWaterScene) {
        LabeledSlider(ParamKeys.WAVE_SPEED, p.waterWaveSpeed, 0.2f..2f, default = SceneParams.DEFAULT.waterWaveSpeed) {
            onChange(p.copy(waterWaveSpeed = it))
        }
        LabeledSlider(ParamKeys.DAMPING, p.waterDamping, 0.9f..0.999f, display = "Calming", default = SceneParams.DEFAULT.waterDamping) {
            onChange(p.copy(waterDamping = it))
        }
        LabeledSlider(
            ParamKeys.RIPPLE_OVERLAY_STRENGTH,
            p.rippleOverlayStrength,
            0f..1f,
            display = "Ripple strength",
            default = SceneParams.DEFAULT.rippleOverlayStrength,
        ) { onChange(p.copy(rippleOverlayStrength = it)) }
        LabeledSlider(ParamKeys.RIPPLE_GLINT, p.rippleOverlaySpecular, 0f..1f, default = SceneParams.DEFAULT.rippleOverlaySpecular) {
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
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CrystalButton(onClick = { host.viewModel.applyCustomShader(source) }) { Text("Apply shader") }
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
