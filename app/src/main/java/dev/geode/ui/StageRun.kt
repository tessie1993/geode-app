package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import dev.geode.cast.CastMirror
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.VisualizerView

/**
 * Runs the stage's view for as long as somebody can see it. GLSurfaceView draws until its host pauses it,
 * and the host used to pause it with the Activity, which froze the visual in a PiP window and on a second
 * screen: Android pauses an Activity there and keeps showing it. The rule is native (core/viz/StageRun);
 * this packs what the shell sees, on the lifecycle callback itself (a stopped Activity recomposes nothing),
 * and hands the same bits to [CastMirror], so a cast knows when the stage has stopped counting Auto's moments.
 */
@Composable
internal fun StageRunEffect(
    view: VisualizerView,
    onSecondScreen: Boolean,
    onDirectorMoment: () -> Unit,
) {
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val activity = LocalContext.current.findActivity()
    val run = remember(view) { StageRun(view) }
    val inPip = VisualizerPipCoordinator.inPictureInPicture
    SideEffect {
        run.pip = inPip
        run.secondScreen = onSecondScreen
        run.restate()
    }
    DisposableEffect(lifecycle, run) {
        val observer =
            LifecycleEventObserver { _, event ->
                // The mode is already set when a move into PiP pauses the Activity; the callback that
                // tells Compose may come later.
                run.pip = activity?.isInPictureInPictureMode ?: run.pip
                run.state = event.targetState
                run.restate()
            }
        CastMirror.onDirectorMoment = onDirectorMoment
        lifecycle.addObserver(observer)
        onDispose {
            lifecycle.removeObserver(observer)
            CastMirror.onDirectorMoment = null
            run.state = Lifecycle.State.DESTROYED
            run.restate()
        }
    }
}

private class StageRun(
    private val view: VisualizerView,
) {
    var state = Lifecycle.State.INITIALIZED
    var pip = false
    var secondScreen = false
    private var draws = false
    private var stated = -1

    fun restate() {
        var flags = 0
        if (state.isAtLeast(Lifecycle.State.STARTED)) flags = flags or GeodeNative.STAGE_STARTED
        if (state.isAtLeast(Lifecycle.State.RESUMED)) flags = flags or GeodeNative.STAGE_RESUMED
        if (pip) flags = flags or GeodeNative.STAGE_PIP
        if (secondScreen) flags = flags or GeodeNative.STAGE_SECOND_SCREEN
        if (flags == stated) return
        stated = flags
        CastMirror.stageFlags = flags
        val now = GeodeNative.stageRun(flags) and GeodeNative.STAGE_DRAWS != 0
        if (now == draws) return
        draws = now
        if (now) view.onResume() else view.onPause()
    }
}
