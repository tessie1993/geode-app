package dev.geode.ui

import androidx.activity.compose.PredictiveBackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import kotlin.coroutines.cancellation.CancellationException

private const val DISMISS_MIN_SCALE = 0.92f
private const val DISMISS_MAX_ALPHA_DROP = 0.4f

class DismissProgress internal constructor() {
    var fraction by mutableFloatStateOf(0f)
        internal set

    val scale: Float get() = 1f - (1f - DISMISS_MIN_SCALE) * fraction

    val alpha: Float get() = 1f - DISMISS_MAX_ALPHA_DROP * fraction
}

fun Modifier.dismissTransform(progress: DismissProgress): Modifier =
    graphicsLayer {
        scaleX = progress.scale
        scaleY = progress.scale
        alpha = progress.alpha
    }

@Composable
fun rememberPredictiveDismiss(
    enabled: Boolean = true,
    onDismiss: () -> Unit,
): DismissProgress {
    val progress = remember { DismissProgress() }
    PredictiveBackHandler(enabled = enabled) { events ->
        try {
            events.collect { event -> progress.fraction = event.progress }
            progress.fraction = 0f
            onDismiss()
        } catch (cancelled: CancellationException) {
            progress.fraction = 0f
            throw cancelled
        }
    }
    return progress
}
