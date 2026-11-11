package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import dev.geode.R
import dev.geode.data.GuiPrefs
import dev.geode.model.ScreenRotation

/** Settings › Look › Layout's screen rotation row and its hint (Visuals › Settings has its own row on the same key). */
@Composable
internal fun ScreenRotationRow(
    gui: GuiPrefs,
    onGuiChange: (GuiPrefs) -> Unit,
) {
    SettingsChoiceRow(
        title = stringResource(R.string.look_screen_rotation),
        options = ScreenRotation.entries,
        selected = gui.screenRotation,
        label = { stringResource(it.labelRes()) },
        onSelect = { onGuiChange(gui.copy(screenRotation = it)) },
    )
    SettingsHint(stringResource(R.string.look_screen_rotation_hint))
}

internal fun ScreenRotation.labelRes(): Int =
    when (this) {
        ScreenRotation.FOLLOW -> R.string.look_screen_rotation_follow
        ScreenRotation.PORTRAIT -> R.string.look_screen_rotation_portrait
        ScreenRotation.LANDSCAPE -> R.string.look_screen_rotation_landscape
    }
