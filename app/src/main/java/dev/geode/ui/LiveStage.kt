package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.retain.RetainObserver
import androidx.compose.runtime.retain.retain
import androidx.compose.ui.platform.LocalContext
import dev.geode.engine.bridge.StageNative
import dev.geode.stage.Stage

/**
 * The live visual's one stage. A dark-mode schedule, a font or display size change, a keyboard or a language
 * recreates the Activity; the stage comes through it, so a recording and a cast drawing from it go on. It
 * closes when the Activity finishes. The app's view is rebuilt over it each time.
 */
@Composable
internal fun rememberLiveStage(): Stage {
    // The application's: a retained value must not hold the Activity it outlives.
    val app = LocalContext.current.applicationContext
    return retainUntilRetired(
        open = { Stage(app, StageNative.STAGE_KIND_VIEW, LIVE_CAP_FPS).apply { castSource = true } },
        close = Stage::close,
    )
}

/** [open]s a value once and keeps it across Activity recreations; [close] runs once nothing will compose it again. */
@Composable
internal fun <T : Any> retainUntilRetired(
    open: () -> T,
    close: (T) -> Unit,
): T = retain { Owned(open(), close) }.value

private class Owned<T : Any>(
    val value: T,
    private val close: (T) -> Unit,
) : RetainObserver {
    override fun onRetained() = Unit

    override fun onEnteredComposition() = Unit

    override fun onExitedComposition() = Unit

    override fun onRetired() = close(value)

    override fun onUnused() = close(value)
}

// Until Settings › Performance's cap lands (VisualizerEngineBindings).
private const val LIVE_CAP_FPS = 60f
