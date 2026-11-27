package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import dev.geode.engine.bridge.PerfNative

/** Settings › Performance's pick as this window's display runs it (`core/viz/frame/LiveFps`); the stage's own rate. */
@Composable
internal fun rememberLiveFps(storedFps: Int): Int {
    val rates = rememberDisplayRefreshRates()
    return remember(storedFps, rates) { PerfNative.liveFpsCap(storedFps, rates) }
}
