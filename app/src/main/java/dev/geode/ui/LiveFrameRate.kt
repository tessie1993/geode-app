package dev.geode.ui

import android.os.Build
import android.view.Display
import android.view.Window
import androidx.activity.compose.LocalActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.preferredFrameRate
import dev.geode.engine.bridge.PerfNative
import kotlin.math.abs

/**
 * The whole app's frame rate while the stage is up: Settings › Performance's pick as this display runs it ([fps],
 * native `core/viz/frame/LiveFps`) and the refresh rate of the display mode that shows it evenly ([refreshHz]).
 * [PerfNative.LIVE_FPS_FOLLOW] and 0 Hz: Follow device, no vote.
 */
@Immutable
data class LiveFrameRate(
    val fps: Int,
    val refreshHz: Float,
) {
    val follows: Boolean get() = fps <= 0 || refreshHz <= 0f
}

/** [LiveFrameRate] for the stored pick on this window's display, read again when the display's modes change. */
@Composable
internal fun rememberLiveFrameRate(storedFps: Int): LiveFrameRate {
    val rates = rememberSeamlessRefreshRates()
    return remember(storedFps, rates) {
        val fps = PerfNative.liveFpsCap(storedFps, rates)
        LiveFrameRate(fps, if (fps > 0) PerfNative.liveFpsRefresh(fps, rates) else 0f)
    }
}

/**
 * The window's vote for [rate] (developer.android.com/media/optimize/performance/frame-rate): the display mode whose
 * refresh rate shows it evenly, as `preferredDisplayModeId`, so the Compose UI and the stage's surface run on the
 * same vsync, and no touch boost while a rate is set (API 35+), which would otherwise pull a 60 Hz pick up to the
 * panel's top mode on every tap. Follow device clears both. The stage's surface votes the same rate on its own
 * (`ANativeWindow_setFrameRateWithChangeStrategy`), and [liveFrameRate] has Compose vote it too.
 */
@Composable
internal fun LiveFrameRateVote(rate: LiveFrameRate) {
    val window = LocalActivity.current?.window ?: return
    val display = LocalView.current.display
    val modeId = remember(display, rate) { display?.let { displayModeFor(it, rate.refreshHz) } ?: 0 }
    DisposableEffect(window, modeId) {
        window.vote(modeId)
        onDispose { window.vote(0) }
    }
}

/** Compose's vote for [rate] (`Modifier.preferredFrameRate`), cast when this content draws; none for Follow device. */
internal fun Modifier.liveFrameRate(rate: LiveFrameRate): Modifier = if (rate.follows) this else preferredFrameRate(rate.fps.toFloat())

private fun Window.vote(modeId: Int) {
    if (attributes.preferredDisplayModeId != modeId) {
        attributes = attributes.apply { preferredDisplayModeId = modeId }
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
        setFrameRateBoostOnTouchEnabled(modeId == 0)
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

/** Pure [displayModeFor]: same size as [current], refresh rate within a hertz of [refreshHz], nearest wins. */
internal fun displayModeFor(
    modes: List<ModeRate>,
    current: ModeRate,
    refreshHz: Float,
): Int {
    if (refreshHz <= 0f) return 0
    return modes
        .filter { it.width == current.width && it.height == current.height && abs(it.refreshHz - refreshHz) <= REFRESH_TOLERANCE_HZ }
        .minByOrNull { abs(it.refreshHz - refreshHz) }
        ?.id ?: 0
}

private const val REFRESH_TOLERANCE_HZ = 1f
