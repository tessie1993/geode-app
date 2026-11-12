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
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.data.VizUiState
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.ParamUnit
import dev.geode.render.scene.SceneParams
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.UiSound

/**
 * Draws a Customize section's controls as core/viz/CustomizeRows hands them over: one row per control that
 * draws on this look and style, with its widget, words, value, range, default, unit and steps. Which widget,
 * which words and whether a control shows at all are native; this draws each row and hands a move to the one
 * write path ([VisualsViewModel.editSceneParams]). Every widget still asks [LocalProLock] whether this user
 * may keep a change, so a Pro control can be tried.
 */
@Composable
internal fun CustomizeControls(
    keys: List<String>,
    host: AdvancedHost,
) {
    // One collector for the whole section, not one per control: a slider drag wakes one section.
    val viz by host.viewModel.vizState.collectAsStateWithLifecycle()
    val liquidMode by host.visualsViewModel.liquidMode.collectAsStateWithLifecycle()
    val sceneId = LocalSceneId.current
    val query = LocalParamQuery.current
    val transition = viz.transitionId != null
    val rows =
        remember(keys, sceneId, query, viz.params, transition, liquidMode) {
            GeodeNative
                .customizeRows(sceneId, keys.joinToString("\n"), query, transition, liquidMode, viz.params.frame())
                .lines()
                .drop(1)
                .filter { it.isNotEmpty() }
                .map { it.split('|') }
        }
    rows.forEach { ControlRow(it, host, viz) }
}

/** One native row (core/viz/CustomizeRows.hpp names its fields), drawn by its type. */
@Composable
private fun ControlRow(
    f: List<String>,
    host: AdvancedHost,
    viz: VizUiState,
) {
    val edit: (Float) -> Unit = { host.visualsViewModel.editSceneParams(viz.params.with(f[2], it)) }
    when (f[0]) {
        "SLIDER" ->
            LabeledSlider(
                label = f[1],
                value = f[4].toFloat(),
                range = f[5].toFloat()..f[6].toFloat(),
                display = f[3],
                scope = ParamScope.UNIVERSAL,
                default = f[7].toFloatOrNull(),
                unit = ParamUnit.entries.getOrNull(f[8].toInt()),
                steps = f[9].toInt(),
                onChange = edit,
            )
        "INT" -> LabeledIntSlider(f[1], f[4].toInt(), f[5].toInt()..f[6].toInt(), f[3], ParamScope.UNIVERSAL) { edit(it.toFloat()) }
        "CHECK" -> CheckRow(f[1], f[4] == "1", f[3], ParamScope.UNIVERSAL) { edit(if (it) 1f else 0f) }
        "CHIPS" -> ParamChips(f[1], f.drop(CHOICES), f[4].toInt(), ParamScope.UNIVERSAL) { edit(it.toFloat()) }
        "PICK" ->
            ProGated(f[1], value = f.getOrNull(CHOICES + f[4].toInt())) {
                DropdownRow(f[3], f.drop(CHOICES), f[4].toInt()) { edit(it.toFloat()) }
            }
        "HINT" -> ControlHint(f[2])
        "NOTE" ->
            Text(
                f[2],
                modifier = Modifier.padding(top = 4.dp, bottom = 2.dp),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error,
            )
        "SHELL" -> ShellSlider(f, host, viz)
        "PIECE" -> PanelPiece(f[1], f[2], f[3], f[4], host, viz)
        else -> Unit
    }
}

/** A slider over a value the shell holds rather than a scene param: the rise, the fall, the transition's length. */
@Composable
private fun ShellSlider(
    f: List<String>,
    host: AdvancedHost,
    viz: VizUiState,
) {
    val range = f[4].toFloat()..f[5].toFloat()
    val unit = ParamUnit.entries.getOrNull(f[6].toInt())
    val vm = host.viewModel
    when (f[2]) {
        "ATTACK" -> LabeledSlider(f[1], viz.attack, range, f[3], ParamScope.UNIVERSAL, unit = unit) { vm.setReactivity(it, viz.decay) }
        "DECAY" -> LabeledSlider(f[1], viz.decay, range, f[3], ParamScope.UNIVERSAL, unit = unit) { vm.setReactivity(viz.attack, it) }
        "TRANSITION" ->
            LabeledSlider(f[1], viz.transitionDurationSec, range, f[3], ParamScope.UNIVERSAL, unit = unit) { vm.setTransitionDuration(it) }
        else -> Unit
    }
}

/**
 * A panel piece that is not one parameter, by its native id: the macro knobs, the moment rows, the route
 * editor, the palette pages. [lockKey] is what Pro gates it by, [label] what Pro and the screen reader name it,
 * [text] its button's words. In a Pro section a free user sees it and a tap opens the paywall.
 */
@Composable
private fun PanelPiece(
    id: String,
    lockKey: String,
    label: String,
    text: String,
    host: AdvancedHost,
    viz: VizUiState,
) {
    val context = LocalContext.current
    val vm = host.visualsViewModel
    val p = viz.params
    val onChange: (SceneParams) -> Unit = vm::editSceneParams
    when (id) {
        "@macros" -> {
            val store = remember { context.geodeContainer.macros }
            val macros by store.state.collectAsStateWithLifecycle()
            MacrosTab(macros, store, viz.sceneId, LocalProLock.current)
        }
        "@macro_editor" -> {
            val store = remember { context.geodeContainer.macros }
            val macros by store.state.collectAsStateWithLifecycle()
            ProGated(lockKey, label) { MacroEditor(macros, store, viz.sceneId) }
        }
        "@palette_maker" -> ProGated(lockKey, label) { YourColoursCard(host, p, onChange, rememberSavedPalettes()) }
        "@moments" -> {
            val reactions by vm.reactions.collectAsStateWithLifecycle()
            val handMask by vm.handMask.collectAsStateWithLifecycle()
            ProGated(lockKey, label) { EventsTab(p, reactions, handMask, vm::editReactions) }
        }
        "@reactions" -> {
            val reactions by vm.reactions.collectAsStateWithLifecycle()
            val handMask by vm.handMask.collectAsStateWithLifecycle()
            ProGated(lockKey, label) { ReactionsSection(p, reactions, handMask, vm::editReactions, vm::rerollReactions) }
        }
        "@notes_key" -> NoteKeyPage(p, onChange)
        "@transition_style" -> TransitionPicker(viz.transitionId, host.viewModel::setTransitionId)
        "@envelopes" -> {
            val adsrs by vm.adsrs.collectAsStateWithLifecycle()
            ProGated(lockKey, label) {
                Column {
                    for (i in 0 until ModCatalog.count("envelopes")) {
                        AdsrCard(index = i, adsrs = adsrs, onChange = { field, value -> vm.setAdsr(i, field, value) })
                    }
                }
            }
        }
        "@waves" -> {
            val lfos by vm.lfos.collectAsStateWithLifecycle()
            ProGated(lockKey, label) {
                Column {
                    TapButton(text) { vm.randomizeWaves() }
                    for (i in 0 until ModCatalog.count("waves")) {
                        ModulatorCard(index = i, lfos = lfos, onChange = { field, value -> vm.setLfo(i, field, value) })
                    }
                }
            }
        }
        "@layers" -> ProGated(lockKey, label) { LayersPage() }
        "@shader_code" -> ProGated(lockKey, label) { ShaderCodePage(host) }
        "@art_palette" -> {
            val artNote by host.viewModel.artPaletteNote.collectAsStateWithLifecycle()
            ProGated(lockKey, label) { TapButton(text) { host.viewModel.applyArtworkPalette() } }
            artNote?.let { ControlHint(it) }
        }
        "@palette_stack" -> PaletteStackCard(p, onChange)
        "@palette_list" -> ProGated(lockKey) { PaletteDropdown(lockKey, p, onChange, rememberSavedPalettes()) }
        "@personality" -> {
            ControlLabelRow(label, lockKey)
            ProGated(lockKey, label) { TapButton(text) { vm.rerollReactions() } }
        }
        else -> Unit
    }
}

/** A tonal button that ticks before it acts. */
@Composable
private fun TapButton(
    text: String,
    onClick: () -> Unit,
) {
    val sounds = LocalUiSounds.current
    FilledTonalButton(onClick = {
        sounds.play(UiSound.TAP)
        onClick()
    }) { Text(text) }
}

/** Where a pick row's choices start (core/viz/CustomizeRows.hpp). */
private const val CHOICES = 5
