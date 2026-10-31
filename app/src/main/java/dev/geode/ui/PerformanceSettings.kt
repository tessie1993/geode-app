package dev.geode.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.GuiPrefs
import dev.geode.geodeContainer
import dev.geode.render.PerformanceMode
import dev.geode.render.PerformanceSettings

/** Settings › Performance: what the live visualizer, wallpaper and dream may spend. Export ignores it. */
@Composable
internal fun PerformanceSettingsTab() {
    val store = LocalContext.current.geodeContainer.performance
    val settings by store.settings.collectAsStateWithLifecycle()
    PerformanceSettingsContent(settings, store::set)
}

@Composable
internal fun PerformanceSettingsContent(
    settings: PerformanceSettings,
    onChange: (PerformanceSettings) -> Unit,
) {
    val mode = settings.mode
    SettingsTabColumn {
        item {
            SettingsGroup(stringResource(R.string.perf_mode_title)) {
                CrystalSegmented(
                    options =
                        listOf(
                            stringResource(R.string.perf_mode_saver),
                            stringResource(R.string.perf_mode_balanced),
                            stringResource(R.string.perf_mode_high),
                        ),
                    selected = mode?.ordinal ?: -1,
                    onSelect = { onChange(PerformanceMode.entries[it].settings) },
                )
                Hint(
                    stringResource(
                        when (mode) {
                            PerformanceMode.SAVER -> R.string.perf_mode_saver_hint
                            PerformanceMode.BALANCED -> R.string.perf_mode_balanced_hint
                            PerformanceMode.HIGH -> R.string.perf_mode_high_hint
                            null -> R.string.perf_mode_custom_hint
                        },
                    ),
                )
            }
        }
        item {
            SettingsGroup(stringResource(R.string.perf_details_title)) {
                Choice(
                    stringResource(R.string.perf_render_scale),
                    PerformanceSettings.RENDER_SCALES.map { "${(it * 100).toInt()}%" },
                    PerformanceSettings.RENDER_SCALES.indexOf(settings.renderScale),
                ) { onChange(settings.copy(renderScale = PerformanceSettings.RENDER_SCALES[it])) }
                Choice(
                    stringResource(R.string.perf_fps_cap),
                    PerformanceSettings.FPS_CAPS.map { stringResource(R.string.perf_fps_value, it) },
                    PerformanceSettings.FPS_CAPS.indexOf(settings.fpsCap),
                ) { onChange(settings.copy(fpsCap = PerformanceSettings.FPS_CAPS[it])) }
                Choice(
                    stringResource(R.string.perf_march),
                    listOf(
                        stringResource(R.string.perf_level_low),
                        stringResource(R.string.perf_level_medium),
                        stringResource(R.string.perf_level_high),
                    ),
                    PerformanceSettings.MARCH_SCALES.indexOf(settings.marchScale),
                ) { onChange(settings.copy(marchScale = PerformanceSettings.MARCH_SCALES[it])) }
                Column {
                    LabeledSwitch(
                        stringResource(R.string.perf_post_fx),
                        checked = settings.postFx,
                        onCheckedChange = { onChange(settings.copy(postFx = it)) },
                    )
                    Hint(stringResource(R.string.perf_post_fx_hint))
                }
                Column {
                    LabeledSwitch(
                        stringResource(R.string.perf_adaptive),
                        checked = settings.adaptive,
                        onCheckedChange = { onChange(settings.copy(adaptive = it)) },
                    )
                    Hint(stringResource(R.string.perf_adaptive_hint))
                }
                Hint(stringResource(R.string.perf_export_note))
            }
        }
    }
}

@Composable
private fun Choice(
    label: String,
    options: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
) {
    Column {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Spacer(Modifier.height(6.dp))
        CrystalSegmented(options = options, selected = selected, onSelect = onSelect)
    }
}

@Composable
private fun Hint(text: String) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

@Preview(name = "Performance · Balanced", showBackground = true, backgroundColor = 0xFF000000)
@Composable
internal fun PerformanceSettingsPreview() {
    CrystalMaterialTheme(GuiPrefs()) {
        PerformanceSettingsContent(PerformanceSettings.BALANCED) {}
    }
}

@Preview(name = "Performance · Custom", showBackground = true, backgroundColor = 0xFF000000)
@Composable
internal fun PerformanceSettingsCustomPreview() {
    CrystalMaterialTheme(GuiPrefs()) {
        PerformanceSettingsContent(PerformanceSettings.SAVER.copy(fpsCap = 60)) {}
    }
}
