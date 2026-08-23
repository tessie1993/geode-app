package dev.geode.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.exportQualityLabel
import dev.geode.export.ExportPresets
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio

@Composable
internal fun ExportSettingsTab(
    exportOpen: Boolean,
    onOpenExport: () -> Unit,
) {
    val context = LocalContext.current
    val store = remember { ExportPrefsStore(GeodePrefsFiles(context).general) }
    var defaults by remember { mutableStateOf(store.load()) }
    LaunchedEffect(exportOpen) { if (!exportOpen) defaults = store.load() }
    val update: (ExportDefaults) -> Unit = {
        defaults = it
        store.save(it)
    }
    SettingsTabColumn {
        item {
            SettingsGroup(stringResource(R.string.export_group_defaults)) {
                Column {
                    Text(stringResource(R.string.export_platform_preset), style = MaterialTheme.typography.labelMedium)
                    Row(Modifier.fillMaxWidth().padding(top = 4.dp).horizontalScroll(rememberScrollState())) {
                        CrystalSegmented(
                            options = ExportPresets.ALL.map { it.name },
                            selected =
                                ExportPresets.indexMatching(
                                    defaults.quality,
                                    defaults.ratio,
                                    defaults.fps,
                                    defaults.loopSafe,
                                ),
                            onSelect = {
                                val preset = ExportPresets.ALL[it]
                                update(
                                    defaults.copy(
                                        quality = preset.quality,
                                        ratio = preset.ratio,
                                        fps = preset.fps,
                                        loopSafe = preset.loopSafe,
                                    ),
                                )
                            },
                        )
                    }
                    Text(
                        presetCaption(
                            defaults,
                            stringResource(R.string.export_spec, defaults.ratio.label, exportQualityLabel(defaults.quality), defaults.fps),
                        ),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Column {
                    Text(stringResource(R.string.export_quality), style = MaterialTheme.typography.labelMedium)
                    CrystalSegmented(
                        options = ExportQuality.entries.map { exportQualityLabel(it) },
                        selected = ExportQuality.entries.indexOf(defaults.quality),
                        onSelect = { update(defaults.copy(quality = ExportQuality.entries[it])) },
                        modifier = Modifier.padding(top = 4.dp),
                    )
                }
                Column {
                    Text(stringResource(R.string.export_frame_rate), style = MaterialTheme.typography.labelMedium)
                    CrystalSegmented(
                        options = listOf(stringResource(R.string.export_fps_30), stringResource(R.string.export_fps_60)),
                        selected = if (defaults.fps == 30) 0 else 1,
                        onSelect = { update(defaults.copy(fps = if (it == 0) 30 else 60)) },
                        modifier = Modifier.padding(top = 4.dp),
                    )
                }
                Column {
                    Text(stringResource(R.string.export_aspect_ratio), style = MaterialTheme.typography.labelMedium)
                    Row(Modifier.fillMaxWidth().padding(top = 4.dp).horizontalScroll(rememberScrollState())) {
                        CrystalSegmented(
                            options = ExportRatio.entries.map { it.label },
                            selected = ExportRatio.entries.indexOf(defaults.ratio),
                            onSelect = { update(defaults.copy(ratio = ExportRatio.entries[it])) },
                        )
                    }
                }
                Column {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            stringResource(R.string.export_loop_safe_default),
                            Modifier.weight(1f),
                            style = MaterialTheme.typography.bodyMedium,
                        )
                        Switch(
                            checked = defaults.loopSafe,
                            onCheckedChange = { update(defaults.copy(loopSafe = it)) },
                        )
                    }
                    Text(
                        stringResource(R.string.export_loop_safe_explainer),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
        item {
            SettingsGroup(stringResource(R.string.studio_render)) {
                CrystalButton(onClick = onOpenExport) { Text(stringResource(R.string.export_video_button)) }
                Text(
                    stringResource(R.string.export_defaults_explainer),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun presetCaption(
    defaults: ExportDefaults,
    baseSpec: String,
): String {
    val spec =
        if (defaults.loopSafe) stringResource(R.string.export_spec_loop_safe, baseSpec) else baseSpec
    return ExportPresets
        .matching(defaults.quality, defaults.ratio, defaults.fps, defaults.loopSafe)
        ?.let { stringResource(R.string.export_named_preset, it.name, spec) }
        ?: stringResource(R.string.export_custom_preset, spec)
}
