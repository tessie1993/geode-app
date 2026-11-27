package dev.geode.ui

import android.hardware.display.DisplayManager
import android.os.Handler
import android.os.Looper
import android.view.View
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.GeneralSettings
import dev.geode.data.PerformanceMode
import dev.geode.data.PerformanceSettings
import dev.geode.engine.bridge.PerfNative
import dev.geode.geodeContainer

/**
 * Settings › Performance › Basics: the quality preset (Saver, Balanced, High), what the live
 * visualizer and dream may spend. Export ignores it.
 */
@Composable
internal fun PerformanceBasics() {
    val store = LocalContext.current.geodeContainer.performance
    val settings by store.settings.collectAsStateWithLifecycle()
    PerformanceModeControls(settings, store::set)
}

/** Settings › Performance › Details: each setting the preset sets, one by one. */
@Composable
internal fun PerformanceDetailsSection() {
    val store = LocalContext.current.geodeContainer.performance
    val settings by store.settings.collectAsStateWithLifecycle()
    PerformanceDetailControls(settings, store::set)
}

/** Both halves together, for the previews. */
@Composable
internal fun PerformanceSettingsContent(
    settings: PerformanceSettings,
    onChange: (PerformanceSettings) -> Unit,
) {
    Column {
        PerformanceModeControls(settings, onChange)
        PerformanceDetailControls(settings, onChange)
    }
}

@Composable
private fun PerformanceModeControls(
    settings: PerformanceSettings,
    onChange: (PerformanceSettings) -> Unit,
) {
    val mode = settings.mode
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
    onChange: (PerformanceSettings) -> Unit,
) {
    Choice(
        stringResource(R.string.perf_render_scale),
        PerformanceSettings.SCALES.map { "${(it * 100).toInt()}%" },
        PerformanceSettings.SCALES.indexOf(settings.renderScale),
    ) { onChange(settings.copy(renderScale = PerformanceSettings.SCALES[it])) }
    val refreshRates = rememberDisplayRefreshRates()
    val fpsCaps = remember(refreshRates) { liveFpsCaps(refreshRates) }
    Column {
        Choice(
            stringResource(R.string.perf_fps_cap),
            fpsCaps.map {
                if (it == PerfNative.LIVE_FPS_FOLLOW) {
                    stringResource(R.string.perf_fps_follow)
                } else {
                    stringResource(R.string.perf_fps_value, it)
                }
            },
            // The stored pick as this display runs it (a 120 picked on a foldable's inner screen reads 60 outside).
            fpsCaps.indexOf(PerfNative.liveFpsCap(settings.fpsCap, refreshRates)),
        ) { onChange(settings.copy(fpsCap = fpsCaps[it])) }
        Spacer(Modifier.height(6.dp))
        Hint(stringResource(R.string.perf_fps_cap_hint))
    }
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

/**
 * The refresh rates of the display under [view] at its current resolution (`Display.getSupportedModes`), ascending:
 * the same set from every mode, so the app's own mode switch never changes it. Empty before the view has a display.
 * Only a read: which rates they offer, how a stored rate snaps to them and which mode a rate asks for are native
 * (`core/viz/frame/LiveFps`).
 */
internal fun displayRefreshRates(view: View): FloatArray {
    val display = view.display ?: return FloatArray(0)
    val current = display.mode
    return display.supportedModes
        .filter { it.physicalWidth == current.physicalWidth && it.physicalHeight == current.physicalHeight }
        .map { it.refreshRate }
        .distinct()
        .sorted()
        .toFloatArray()
}

/**
 * [displayRefreshRates] of this window's display: read at start, again when the configuration changes (a fold, a
 * move), and whenever the display says its modes changed (`DisplayManager.DisplayListener`).
 */
@Composable
internal fun rememberDisplayRefreshRates(): FloatArray {
    val view = LocalView.current
    val configuration = LocalConfiguration.current
    var rates by remember(view, configuration) { mutableStateOf(displayRefreshRates(view)) }
    DisposableEffect(view, configuration) {
        val displays = view.context.getSystemService(DisplayManager::class.java)
        val listener =
            object : DisplayManager.DisplayListener {
                override fun onDisplayAdded(displayId: Int) = Unit

                override fun onDisplayRemoved(displayId: Int) = Unit

                override fun onDisplayChanged(displayId: Int) {
                    if (displayId != view.display?.displayId) return
                    val now = displayRefreshRates(view)
                    if (!now.contentEquals(rates)) rates = now
                }
            }
        displays?.registerDisplayListener(listener, Handler(Looper.getMainLooper()))
        onDispose { displays?.unregisterDisplayListener(listener) }
    }
    return rates
}

/** The live caps Settings › Performance offers on a display with [refreshRates], ascending, then Follow device (native). */
private fun liveFpsCaps(refreshRates: FloatArray): List<Int> {
    val out = IntArray(PerfNative.LIVE_FPS_MAX_CHOICES)
    val count = PerfNative.liveFpsChoices(refreshRates, out)
    return out.take(count.coerceIn(0, out.size))
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
        PerformanceSettingsContent(PerformanceSettings.BALANCED) {}
    }
}

@Preview(name = "Performance · Custom", showBackground = true, backgroundColor = 0xFF000000)
@Composable
internal fun PerformanceSettingsCustomPreview() {
    CrystalMaterialTheme(GeneralSettings()) {
        PerformanceSettingsContent(PerformanceSettings.SAVER.copy(fpsCap = 60)) {}
    }
}
