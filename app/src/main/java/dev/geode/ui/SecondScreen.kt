package dev.geode.ui

import android.app.Presentation
import android.content.Context
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.view.Display
import android.view.ViewGroup
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import dev.geode.render.VisualizerView

@Composable
fun rememberExternalDisplay(): Display? {
    val context = LocalContext.current
    var display by remember { mutableStateOf(firstPresentationDisplay(context)) }
    DisposableEffect(Unit) {
        val manager = context.getSystemService(DisplayManager::class.java)
        val listener =
            object : DisplayManager.DisplayListener {
                override fun onDisplayAdded(displayId: Int) {
                    display = firstPresentationDisplay(context)
                }

                override fun onDisplayRemoved(displayId: Int) {
                    display = firstPresentationDisplay(context)
                }

                override fun onDisplayChanged(displayId: Int) {
                    display = firstPresentationDisplay(context)
                }
            }
        manager?.registerDisplayListener(listener, null)
        onDispose { manager?.unregisterDisplayListener(listener) }
    }
    return display
}

private fun firstPresentationDisplay(context: Context): Display? =
    context
        .getSystemService(DisplayManager::class.java)
        ?.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
        ?.firstOrNull()

@Composable
fun SecondScreenCanvas(
    display: Display,
    view: VisualizerView,
) {
    val context = LocalContext.current
    DisposableEffect(display, view) {
        val presentation =
            object : Presentation(context, display) {
                override fun onCreate(savedInstanceState: Bundle?) {
                    super.onCreate(savedInstanceState)
                    (view.parent as? ViewGroup)?.removeView(view)
                    setContentView(view)
                }
            }
        val shown = runCatching { presentation.show() }.isSuccess
        onDispose {
            if (shown) runCatching { presentation.dismiss() }
            (view.parent as? ViewGroup)?.removeView(view)
        }
    }
}
