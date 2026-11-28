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
import dev.geode.data.GeneralSettings
import dev.geode.data.PerformanceMode
import dev.geode.data.PerformanceSettings
import dev.geode.data.ResolutionMirror
import dev.geode.engine.bridge.PerfNative
import dev.geode.geodeContainer

/**
 * Settings › Performance › Basics: the quality preset (Saver, Balanced, High), what the live
 * visualizer may spend. Export ignores it.
 */
@Composable
internal fun PerformanceBasics() {
    val store = LocalContext.current.geodeContainer.performance
    val settings by store.settings.collectAsStateWithLifecycle()
    val resolution by store.resolution.collectAsStateWithLifecycle()
    PerformanceModeControls(settings, resolution, store::set)
}

/** Settings › Performance › Details: each setting the preset sets, one by one. */
@Composable
internal fun PerformanceDetailsSection() {
    val store = LocalContext.current.geodeContainer.performance
    val settings by store.settings.collectAsStateWithLifecycle()
    val resolution by store.resolution.collectAsStateWithLifecycle()
    PerformanceDetailControls(settings, resolution, store::set)
}

/** Both halves together, for the previews. */
@Composable
internal fun PerformanceSettingsContent(
    settings: PerformanceSettings,
    onChange: (PerformanceSettings) -> Unit,
    resolution: ResolutionMirror = ResolutionMirror.EMPTY,
) {
    Column {
        PerformanceModeControls(settings, resolution, onChange)
        PerformanceDetailControls(settings, resolution, onChange)
    }
}

@Composable
private fun PerformanceModeControls(
    settings: PerformanceSettings,
    resolution: ResolutionMirror,
    onChange: (PerformanceSettings) -> Unit,
) {
    val mode = resolution.mode
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

@Composable
private fun PerformanceDetailControls(
    settings: PerformanceSettings,
    resolution: ResolutionMirror,
    onChange: (PerformanceSettings) -> Unit,
) {
    ResolutionControls(settings, resolution, onChange)
    Choice(
        stringResource(R.string.perf_march),
        listOf(
            stringResource(R.string.perf_level_low),
            stringResource(R.string.perf_level_medium),
            stringResource(R.string.perf_level_high),
        ),
        PerformanceSettings.SCALES.indexOf(settings.marchScale),
    ) { onChange(settings.copy(marchScale = PerformanceSettings.SCALES[it])) }
    Column {
        LabeledSwitch(
            stringResource(R.string.perf_post_fx),
            checked = settings.postFx,
            onCheckedChange = { onChange(settings.copy(postFx = it)) },
        )
        Hint(stringResource(R.string.perf_post_fx_hint))
    }
    Hint(stringResource(R.string.perf_export_note))
}

/** The render resolutions native offers on this panel, Native last, and under them what the pick draws on the screen's current mode. */
@Composable
private fun ResolutionControls(
    settings: PerformanceSettings,
    resolution: ResolutionMirror,
    onChange: (PerformanceSettings) -> Unit,
) {
    Column {
        Choice(
            stringResource(R.string.perf_render_scale),
            resolution.steps.map {
                if (it == PerfNative.PERF_NATIVE) {
                    stringResource(R.string.perf_resolution_native)
                } else {
                    stringResource(R.string.perf_resolution_value, it)
                }
            },
            resolution.selected,
        ) { onChange(settings.copy(resolution = resolution.steps[it])) }
        if (resolution.drawnWidth > 0) {
            Spacer(Modifier.height(6.dp))
            Hint(
                stringResource(
                    R.string.perf_resolution_hint,
                    resolution.drawnWidth,
                    resolution.drawnHeight,
                    resolution.modeWidth,
                    resolution.modeHeight,
                ),
            )
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
    CrystalMaterialTheme(GeneralSettings()) {
        PerformanceSettingsContent(PerformanceSettings.BALANCED, onChange = {})
    }
}

@Preview(name = "Performance · Custom", showBackground = true, backgroundColor = 0xFF000000)
@Composable
internal fun PerformanceSettingsCustomPreview() {
    CrystalMaterialTheme(GeneralSettings()) {
        PerformanceSettingsContent(PerformanceSettings.SAVER.copy(marchScale = 1.0f), onChange = {})
    }
}
