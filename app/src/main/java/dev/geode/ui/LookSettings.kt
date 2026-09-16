package dev.geode.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.BootAnimationStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.ui.glass.GlassSegmented
import dev.geode.ui.glass.GlassSlider
import dev.geode.ui.glass.GlassToggle

/**
 * Settings › Look.
 *
 * The glass theme (`ui/glass/`) is the only theme now, so there is no pack picker, corner style
 * or font colour here any more — just the tokens the liquid-glass system reads
 * ([GuiPrefs.glassTint], [GuiPrefs.glassOpacity], [GuiPrefs.bubbleDensity],
 * [GuiPrefs.liquidMotion]) plus the layout knobs that survived from before.
 */
@Composable
internal fun LookSettingsTab(viewModel: SettingsViewModel) {
    val gui by viewModel.guiPrefs.collectAsStateWithLifecycle()
    SettingsTabColumn {
        item {
            SettingsGroup(stringResource(R.string.look_group_theme)) {
                LabeledSlider(
                    stringResource(R.string.look_glass_tint, (gui.glassTint * 100).toInt()),
                    gui.glassTint,
                    0f..1f,
                ) { viewModel.setGuiPrefs(gui.copy(glassTint = it)) }
                Text(
                    stringResource(R.string.look_glass_tint_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                LabeledSlider(
                    stringResource(R.string.look_glass_opacity, (gui.glassOpacity * 100).toInt()),
                    gui.glassOpacity,
                    0.1f..0.6f,
                ) { viewModel.setGuiPrefs(gui.copy(glassOpacity = it)) }
                LabeledSlider(
                    stringResource(R.string.look_accent_intensity, (gui.accentIntensity * 100).toInt()),
                    gui.accentIntensity,
                    0.5f..1.5f,
                ) { viewModel.setGuiPrefs(gui.copy(accentIntensity = it)) }
                LabeledSlider(
                    stringResource(R.string.look_background_dim, (gui.backgroundDim * 100).toInt()),
                    gui.backgroundDim,
                    0f..0.6f,
                ) { viewModel.setGuiPrefs(gui.copy(backgroundDim = it)) }
                ToggleRow(stringResource(R.string.look_follow_system), gui.followSystemDark) {
                    viewModel.setGuiPrefs(gui.copy(followSystemDark = it))
                }
            }
        }
        item {
            SettingsGroup(stringResource(R.string.look_group_text)) {
                Column {
                    Text(
                        stringResource(R.string.look_text_size, (gui.textScale * 100).toInt()),
                        style = MaterialTheme.typography.labelMedium,
                    )
                    GlassSlider(
                        value = gui.textScale,
                        onValueChange = { viewModel.setGuiPrefs(gui.copy(textScale = it)) },
                        valueRange = GuiPrefs.TEXT_SCALE_MIN..GuiPrefs.TEXT_SCALE_MAX,
                    )
                    Text(
                        stringResource(R.string.look_text_size_hint),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
        item {
            SettingsGroup(stringResource(R.string.look_group_motion)) {
                LabeledSlider(
                    stringResource(R.string.look_bubble_density, (gui.bubbleDensity * 100).toInt()),
                    gui.bubbleDensity,
                    0f..1f,
                ) { viewModel.setGuiPrefs(gui.copy(bubbleDensity = it)) }
                Text(
                    stringResource(R.string.look_bubble_density_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                LabeledSlider(
                    stringResource(R.string.look_liquid_motion, (gui.liquidMotion * 100).toInt()),
                    gui.liquidMotion,
                    0f..1f,
                ) { viewModel.setGuiPrefs(gui.copy(liquidMotion = it)) }
                Text(
                    stringResource(R.string.look_liquid_motion_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                ToggleRow(stringResource(R.string.look_reduced_motion), gui.reducedMotion) {
                    viewModel.setGuiPrefs(gui.copy(reducedMotion = it))
                }
                Text(
                    stringResource(R.string.look_reduced_motion_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        item { SettingsGroup(stringResource(R.string.look_group_layout)) { LayoutGroup(viewModel, gui) } }
    }
}

@Composable
private fun LayoutGroup(
    viewModel: SettingsViewModel,
    gui: GuiPrefs,
) {
    LabeledSlider(
        stringResource(R.string.look_bar_opacity, (gui.barOpacity * 100).toInt()),
        gui.barOpacity,
        0.2f..1f,
    ) { viewModel.setGuiPrefs(gui.copy(barOpacity = it)) }
    Column {
        Text(stringResource(R.string.look_player_position), style = MaterialTheme.typography.labelMedium)
        GlassSegmented(
            options = PlayerPosition.entries.map { stringResource(it.labelRes) },
            selected = PlayerPosition.entries.indexOf(gui.playerPosition),
            onSelect = { viewModel.setGuiPrefs(gui.copy(playerPosition = PlayerPosition.entries[it])) },
        )
    }
    ToggleRow(stringResource(R.string.look_compact_player), gui.compactPlayer) {
        viewModel.setGuiPrefs(gui.copy(compactPlayer = it))
    }
    Column {
        ToggleRow(stringResource(R.string.look_clear_visuals_menu), gui.clearVisualsMenu) {
            viewModel.setGuiPrefs(gui.copy(clearVisualsMenu = it))
        }
        Text(
            stringResource(R.string.look_clear_visuals_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    BootAnimationRow()
}

@Composable
private fun BootAnimationRow() {
    val ctx = LocalContext.current
    val store = remember { BootAnimationStore(GeodePrefsFiles(ctx).general) }
    var bootAnim by remember { mutableStateOf(store.load()) }
    ToggleRow(stringResource(R.string.look_boot_animation), bootAnim) {
        bootAnim = it
        store.save(it)
    }
}

@Composable
private fun LabeledSlider(
    label: String,
    value: Float,
    valueRange: ClosedFloatingPointRange<Float>,
    onValueChange: (Float) -> Unit,
) {
    Column {
        Text(label, style = MaterialTheme.typography.labelMedium)
        GlassSlider(value = value, onValueChange = onValueChange, valueRange = valueRange)
    }
}

@Composable
private fun ToggleRow(
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        GlassToggle(checked = checked, onCheckedChange = onCheckedChange)
    }
}
