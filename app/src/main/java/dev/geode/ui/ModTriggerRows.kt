package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.geodeContainer

/**
 * Which Quick knob a wave or an envelope on "Macro knob" turns: the knobs on screen, by name, as native lists them
 * (the macro editor's read). [knob] is the knob's id, 0 for none; a knob since deleted reads as none.
 */
@Composable
internal fun MacroKnobRow(
    knob: Int,
    onPick: (Int) -> Unit,
) {
    val context = LocalContext.current
    val store = remember { context.geodeContainer.macros }
    val bank by store.state.collectAsStateWithLifecycle()
    val sceneId = LocalSceneId.current
    val knobs = remember(bank, sceneId) { store.editor(sceneId).knobs }
    val name = stringResource(R.string.mod_macro_knob)
    val none = stringResource(R.string.mod_macro_knob_none)
    DropdownRow(name, knobs.map { it.name }, knobs.indexOfFirst { it.id == knob }, placeholder = none) { onPick(knobs[it].id) }
}

/**
 * "On a moment": none (a wave runs free, an envelope fires on a note start) or one of Customize › Music's moments,
 * by the same ordinal native's Moment has ([MusicMoment]). What a moment is and when it comes is native.
 */
@Composable
internal fun MomentTriggerRow(
    moment: Int,
    none: String,
    onPick: (Int) -> Unit,
) {
    val options = listOf(none) + MusicMoment.entries.map { stringResource(it.label) }
    DropdownRow(stringResource(R.string.mod_on_moment), options, (moment + 1).coerceIn(0, options.lastIndex)) { onPick(it - 1) }
}
