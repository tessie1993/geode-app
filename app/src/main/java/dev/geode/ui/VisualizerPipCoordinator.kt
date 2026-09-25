package dev.geode.ui

import android.graphics.Rect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** State shared by the immersive visualizer and its activity PiP host. */
internal object VisualizerPipCoordinator {
    var visualizerShowing by mutableStateOf(false)

    var inPictureInPicture by mutableStateOf(false)

    /** Window coordinates of the actual visualizer surface for PiP's source rectangle. */
    var canvasBoundsPx: Rect? = null
}
