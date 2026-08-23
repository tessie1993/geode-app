package dev.geode.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R

@Composable
internal fun BehaviorSettingsTab(viewModel: SettingsViewModel) {
    val playerViewModel: PlayerViewModel = geodeViewModel()
    val gui by viewModel.guiPrefs.collectAsStateWithLifecycle()
    SettingsTabColumn {
        item { SettingsGroup(stringResource(R.string.behavior_group_intent)) { IntentGroup(viewModel, gui) } }
        item { SettingsGroup(stringResource(R.string.behavior_group_touch)) { TouchGroup(viewModel, gui) } }
        item { SettingsGroup(stringResource(R.string.behavior_group_display)) { ConnectedDisplayGroup(viewModel, gui) } }
        item { SettingsGroup(stringResource(R.string.behavior_group_safety)) { VisualSafetyGroup(viewModel, gui) } }
        item { SettingsGroup(stringResource(R.string.behavior_group_auto)) { AutoVisualsGroup(playerViewModel) } }
        item { SettingsGroup(stringResource(R.string.behavior_group_wallpaper)) { LiveWallpaperGroup() } }
    }
}

/** The first-run question, asked again. Changing it moves Studio in or out of the navigation. */
@Composable
private fun IntentGroup(
    viewModel: SettingsViewModel,
    gui: GuiPrefs,
) {
    Column {
        Text(stringResource(R.string.settings_intent_title), style = MaterialTheme.typography.bodyMedium)
        CrystalSegmented(
            options = UserIntent.entries.map { stringResource(it.labelRes) },
            selected = UserIntent.entries.indexOf(gui.intent ?: UserIntent.BOTH),
            onSelect = { viewModel.setGuiPrefs(gui.copy(intent = UserIntent.entries[it])) },
            modifier = Modifier.padding(top = 4.dp),
        )
        Text(
            stringResource((gui.intent ?: UserIntent.BOTH).detailRes),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun TouchGroup(
    viewModel: SettingsViewModel,
    gui: GuiPrefs,
) {
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.behavior_touch_smear), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
            Switch(
                checked = gui.touchSmear,
                onCheckedChange = { viewModel.setGuiPrefs(gui.copy(touchSmear = it)) },
            )
        }
        Text(
            stringResource(R.string.behavior_touch_smear_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (gui.touchSmear) {
            Text(
                stringResource(R.string.behavior_smear_strength, (gui.touchSmearStrength * 100).toInt()),
                style = MaterialTheme.typography.labelMedium,
            )
            CrystalSlider(
                value = gui.touchSmearStrength,
                onValueChange = { viewModel.setGuiPrefs(gui.copy(touchSmearStrength = it)) },
                valueRange = 0.2f..2f,
            )
        }
    }
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.behavior_touch_transform), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
            Switch(
                checked = gui.touchTransform,
                onCheckedChange = { viewModel.setGuiPrefs(gui.copy(touchTransform = it)) },
            )
        }
        Text(
            stringResource(R.string.behavior_touch_transform_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun ConnectedDisplayGroup(
    viewModel: SettingsViewModel,
    gui: GuiPrefs,
) {
    val external = rememberExternalDisplay()
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.behavior_display_use), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
            Switch(
                checked = gui.secondScreen,
                onCheckedChange = { viewModel.setGuiPrefs(gui.copy(secondScreen = it)) },
            )
        }
        Text(
            if (external != null) {
                stringResource(R.string.behavior_display_connected, external.name)
            } else {
                stringResource(R.string.behavior_display_none)
            },
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun VisualSafetyGroup(
    viewModel: SettingsViewModel,
    gui: GuiPrefs,
) {
    Column {
        Text(stringResource(R.string.behavior_safety_title), style = MaterialTheme.typography.bodyMedium)
        // Stated, not offered. The flash clamp is unconditional in the render path, so the only
        // honest thing this screen can do about it is say so.
        Text(
            stringResource(R.string.behavior_flash_guidance),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.behavior_slow_motion), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
            Switch(
                checked = gui.reducedMotion,
                onCheckedChange = { viewModel.setGuiPrefs(gui.copy(reducedMotion = it)) },
            )
        }
        Text(
            stringResource(R.string.behavior_slow_motion_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun LiveWallpaperGroup() {
    val ctx = LocalContext.current
    Text(
        stringResource(R.string.behavior_wallpaper_hint),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    CrystalButton(onClick = {
        val direct =
            android.content.Intent(
                android.app.WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER,
            ).putExtra(
                android.app.WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT,
                android.content.ComponentName(
                    ctx,
                    dev.geode.wallpaper.VisualizerWallpaperService::class.java,
                ),
            )
        val ok = runCatching { ctx.startActivity(direct) }.isSuccess
        if (!ok) {
            runCatching {
                ctx.startActivity(android.content.Intent(android.content.Intent.ACTION_SET_WALLPAPER))
            }
        }
    }) { Text(stringResource(R.string.behavior_wallpaper_button)) }
}
