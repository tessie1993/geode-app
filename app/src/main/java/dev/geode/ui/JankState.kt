package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalView
import androidx.metrics.performance.PerformanceMetricsState

@Composable
private fun rememberMetricsStateHolder(): PerformanceMetricsState.Holder {
    val view = LocalView.current
    return remember(view) { PerformanceMetricsState.getHolderForHierarchy(view) }
}

@Composable
internal fun JankState(
    key: String,
    value: String?,
) {
    val holder = rememberMetricsStateHolder()
    DisposableEffect(holder, key, value) {
        val state = holder.state
        if (value != null) state?.putState(key, value)
        onDispose { state?.removeState(key) }
    }
}
