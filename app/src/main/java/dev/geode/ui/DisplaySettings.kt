package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.ui.theme.rememberSystemReducedMotion

/**
 * Settings › Display & performance: what the live visual may spend, how the app looks, the
 * screens around it (background image, a connected display, picture-in-picture), and
 * Accessibility (text, motion and the flash limit).
 */
@Composable
internal fun DisplaySettingsPage(viewModel: SettingsViewModel) {
    val gui by viewModel.guiPrefs.collectAsStateWithLifecycle()
    val (performance, setPerformance) = rememberPerformanceSettings()
    var backgroundOpen by rememberSaveable { mutableStateOf(false) }
    SettingsTabColumn {
        performanceItems(performance, setPerformance)
        item { SettingsGroup(stringResource(R.string.settings_display_appearance)) { AppearanceSettings(viewModel, gui) } }
        item {
            SettingsGroup(stringResource(R.string.settings_display_screens)) {
                SettingsRow(
                    title = stringResource(R.string.background_sheet_title),
                    summary = stringResource(R.string.settings_display_background_summary),
                    onClick = { backgroundOpen = true },
                )
                val external = rememberExternalDisplay()
                SettingsSwitchRow(
                    title = stringResource(R.string.behavior_display_use),
                    summary =
                        if (external != null) {
                            stringResource(R.string.behavior_display_connected, external.name)
                        } else {
                            stringResource(R.string.behavior_display_none)
                        },
                    checked = gui.secondScreen,
                    onCheckedChange = { viewModel.setGuiPrefs(gui.copy(secondScreen = it)) },
                )
                // Whether swiping home from the fullscreen visual during playback drops into PiP on its own.
                SettingsSwitchRow(
                    title = stringResource(R.string.behavior_pip_auto_enter),
                    summary = stringResource(R.string.behavior_pip_auto_enter_hint),
                    checked = gui.autoEnterPip,
                    onCheckedChange = { viewModel.setGuiPrefs(gui.copy(autoEnterPip = it)) },
                )
            }
        }
        item {
            SettingsGroup(stringResource(R.string.settings_display_accessibility)) {
                TextSettings(viewModel, gui)
                // Android's "Remove animations" wins: the theme already follows it live
                // (CrystalMaterialTheme), so the switch shows it on and cannot be turned off.
                val system = rememberSystemReducedMotion()
                SettingsSwitchRow(
                    title = stringResource(R.string.behavior_slow_motion),
                    summary =
                        stringResource(
                            if (system) R.string.settings_reduce_motion_system else R.string.behavior_slow_motion_hint,
                        ),
                    checked = gui.reducedMotion || system,
                    enabled = !system,
                    onCheckedChange = { viewModel.setGuiPrefs(gui.copy(reducedMotion = it)) },
                )
                // Stated, not offered. The flash clamp is unconditional in the render path, so the
                // only honest thing this screen can do about it is say so.
                SettingsRow(
                    title = stringResource(R.string.behavior_safety_title),
                    summary = stringResource(R.string.behavior_flash_guidance),
                )
            }
        }
    }
    if (backgroundOpen) BackgroundSheet(onDismiss = { backgroundOpen = false })
}
