package dev.geode.ui

import android.os.Build
import android.view.Display
import android.view.Window
import androidx.activity.compose.LocalActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.preferredFrameRate
import dev.geode.engine.bridge.PerfNative

/**
 * The whole app's frame rate: Settings › Performance's pick as this display runs it ([fps]) and the refresh rate of
 * the display mode that shows it evenly ([refreshHz]), both native (`core/viz/frame/LiveFps`). 0 Hz: no vote
 * (Follow device).
 */
@Immutable
data class LiveFrameRate(
    val fps: Int,
    val refreshHz: Float,
) {
    val follows: Boolean get() = refreshHz <= 0f
}

/** [LiveFrameRate] for the stored pick on this window's display, read again when the display's modes change. */
@Composable
internal fun rememberLiveFrameRate(storedFps: Int): LiveFrameRate {
    val rates = rememberDisplayRefreshRates()
    return remember(storedFps, rates) {
        val fps = PerfNative.liveFpsCap(storedFps, rates)
        LiveFrameRate(fps, PerfNative.liveFpsRefresh(fps, rates))
    }
}

/**
 * The window's vote (developer.android.com/media/optimize/performance/frame-rate): the display mode that shows the
 * rate evenly, and no touch boost while one is set (API 35+), which would pull a 60 Hz pick up to the panel's top
 * mode on every tap. Cast only on a change; Follow device, and leaving, clear both.
 */
@Composable
internal fun LiveFrameRateVote(rate: LiveFrameRate) {
    val window = LocalActivity.current?.window ?: return
    val display = LocalView.current.display
    val modeId = remember(display, rate) { display?.let { displayModeFor(it, rate.refreshHz) } ?: 0 }
    LaunchedEffect(window, modeId) { window.vote(modeId) }
    DisposableEffect(window) { onDispose { window.vote(0) } }
}

/** Compose's vote for [rate] (`Modifier.preferredFrameRate`), cast when this content draws; none for Follow device. */
internal fun Modifier.liveFrameRate(rate: LiveFrameRate): Modifier = if (rate.follows) this else preferredFrameRate(rate.fps.toFloat())

private fun Window.vote(modeId: Int) {
    if (attributes.preferredDisplayModeId != modeId) {
        attributes = attributes.apply { preferredDisplayModeId = modeId }
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
        val boost = modeId == 0
        if (getFrameRateBoostOnTouchEnabled() != boost) setFrameRateBoostOnTouchEnabled(boost)
    }
}

/** The mode of [display] at its current resolution whose refresh rate is [refreshHz]; 0 (no preference) for none. */
internal fun displayModeFor(
    display: Display,
    refreshHz: Float,
): Int {
    val current = display.mode
    return displayModeFor(
        display.supportedModes.map { ModeRate(it.modeId, it.physicalWidth, it.physicalHeight, it.refreshRate) },
        ModeRate(current.modeId, current.physicalWidth, current.physicalHeight, current.refreshRate),
        refreshHz,
    )
}

/** A display mode's id, size and refresh rate. */
internal data class ModeRate(
    val id: Int,
    val width: Int,
    val height: Int,
    val refreshHz: Float,
)

/**
 * Pure [displayModeFor]: the mode at [current]'s size whose refresh rate is [refreshHz], which native chose from these
 * same modes' rates (`displayRefreshRates`); 0 for none.
 */
internal fun displayModeFor(
    modes: List<ModeRate>,
    current: ModeRate,
    refreshHz: Float,
): Int {
    if (refreshHz <= 0f) return 0
    return modes.firstOrNull { it.width == current.width && it.height == current.height && it.refreshHz == refreshHz }?.id ?: 0
}
