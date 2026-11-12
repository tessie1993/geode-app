package dev.geode.ui

import android.widget.Toast
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.repeatOnLifecycle
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.stage.StageView
import kotlinx.coroutines.delay

/**
 * "Phone is warm: 30 fps", once per process, the first time heat drops the stage's frame rate (#994).
 * Whether to say it is native (`ThermalState::takeWarmNote`); this only asks while the stage is in view
 * and says it as a toast, which TalkBack reads out and which moves only as the system's animations do.
 */
@Composable
internal fun StageWarmNote(
    visualizerView: StageView,
    pip: Boolean,
) {
    val context = LocalContext.current
    val resources = LocalResources.current
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    LaunchedEffect(visualizerView, pip, lifecycle) {
        if (pip) return@LaunchedEffect
        lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) {
            while (true) {
                val stage = visualizerView.stage
                val fps = GeodeNative.thermalTakeWarmNote(stage.capFps, stage.pacedFps())
                if (fps > 0) {
                    Toast.makeText(context, resources.getString(R.string.stage_phone_warm, fps), Toast.LENGTH_LONG).show()
                    break
                }
                delay(WARM_NOTE_POLL_MS)
            }
        }
    }
}

/** The thermal state settles once a second; asking every two is soon enough for a note. */
private const val WARM_NOTE_POLL_MS = 2_000L
