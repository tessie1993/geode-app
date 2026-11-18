package dev.geode.ui

import android.view.View
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
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
import kotlinx.coroutines.delay
import kotlin.math.roundToInt

/**
 * Settings › Performance › Basics: the quality preset (Saver, Balanced, High), what the live
 * visualizer, wallpaper and dream may spend. Export ignores it.
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
    val refreshRates = rememberSeamlessRefreshRates()
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
    Column {
        LabeledSwitch(
            stringResource(R.string.perf_adaptive),
            checked = settings.adaptive,
            onCheckedChange = { onChange(settings.copy(adaptive = it)) },
        )
        Hint(stringResource(R.string.perf_adaptive_hint))
        if (settings.adaptive) AdaptiveNow(settings)
    }
    Hint(stringResource(R.string.perf_export_note))
}

/**
 * "Adapt to frame time"'s current step, read back from native once a second (`viz/frame/FrameGovernor`, the surface
 * that drew last): "Now: 60 fps, full quality", or what it has lowered under [settings]. Nothing before a live frame.
 */
@Composable
private fun AdaptiveNow(settings: PerformanceSettings) {
    val now by produceState<FloatArray?>(null) {
        while (true) {
            val out = FloatArray(PerfNative.PERF_ADAPT_NOW_FLOATS)
            value = out.takeIf { runCatching { PerfNative.perfAdaptNow(out) }.getOrDefault(false) }
            delay(ADAPTIVE_NOW_POLL_MS)
        }
    }
    val v = now ?: return
    val fps = v[0].roundToInt()
    if (v[4] < 0f || fps <= 0) return
    val lowered =
        buildList {
            if (v[1] < settings.renderScale) add(stringResource(R.string.perf_adaptive_now_scale, percent(v[1])))
            if (v[2] < settings.marchScale) add(stringResource(R.string.perf_adaptive_now_march, percent(v[2])))
            if (v[3] == 0f && settings.postFx) add(stringResource(R.string.perf_adaptive_now_no_fx))
        }
    Hint(
        when {
            v[4] == 0f -> stringResource(R.string.perf_adaptive_now, fps, stringResource(R.string.perf_adaptive_now_full))
            lowered.isEmpty() -> stringResource(R.string.perf_adaptive_now_fps, fps)
            else -> stringResource(R.string.perf_adaptive_now, fps, lowered.joinToString(", "))
        },
    )
}

private fun percent(scale: Float): String = "${(scale * 100).roundToInt()}%"

private const val ADAPTIVE_NOW_POLL_MS = 1_000L

/**
 * The refresh rates the display under [view] can switch to without a blank: its current mode's and that mode's
 * seamless alternatives (`Display.Mode.getAlternativeRefreshRates`), the set the pacer's two-argument
 * `Surface.setFrameRate` can reach. Empty before the view has a display. Only a read: which caps they allow,
 * and how a stored cap snaps to them, is native (`core/viz/frame/LiveFps`).
 */
internal fun seamlessRefreshRates(view: View): FloatArray {
    val mode = view.display?.mode ?: return FloatArray(0)
    return floatArrayOf(mode.refreshRate) + mode.alternativeRefreshRates
}

/** [seamlessRefreshRates] of this window's display, read again when the configuration changes (a fold, a move). */
@Composable
internal fun rememberSeamlessRefreshRates(): FloatArray {
    val view = LocalView.current
    val configuration = LocalConfiguration.current
    return remember(view, configuration) { seamlessRefreshRates(view) }
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
