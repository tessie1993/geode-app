package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import dev.geode.R
import dev.geode.data.GeneralSettings
import dev.geode.model.ScreenRotation

/** Settings › Look › Layout's screen rotation row and its hint (Visuals › Settings has its own row on the same key). */
@Composable
internal fun ScreenRotationRow(
    general: GeneralSettings,
    onGeneralChange: ((GeneralSettings) -> GeneralSettings) -> Unit,
) {
    SettingsChoiceRow(
        title = stringResource(R.string.look_screen_rotation),
        options = ScreenRotation.entries,
        selected = general.screenRotation,
        label = { stringResource(it.labelRes()) },
        onSelect = { rotation -> onGeneralChange { it.copy(screenRotation = rotation) } },
    )
    SettingsHint(stringResource(R.string.look_screen_rotation_hint))
}

internal fun ScreenRotation.labelRes(): Int =
    when (this) {
        ScreenRotation.FOLLOW -> R.string.look_screen_rotation_follow
        ScreenRotation.PORTRAIT -> R.string.look_screen_rotation_portrait
        ScreenRotation.LANDSCAPE -> R.string.look_screen_rotation_landscape
    }
