package dev.geode.data

import android.content.Context
import android.hardware.display.DisplayManager
import android.view.Display

/**
 * What Android says about a display's pixels, handed to native as is (`core/viz/frame/Quality` decides): the panel,
 * its largest physical mode (`Display.Mode.getPhysicalWidth/Height` over `getSupportedModes`), and the mode it runs
 * now. Physical sizes, not the window's: a panel running a lower mode (a 1440p phone set to FHD+) shows the window
 * upscaled. In the panel's natural orientation; native reads only the short side and the ratio. 0 for unknown.
 */
data class DisplayPanel(
    val panelWidth: Int = 0,
    val panelHeight: Int = 0,
    val modeWidth: Int = 0,
    val modeHeight: Int = 0,
) {
    companion object {
        fun of(display: Display?): DisplayPanel {
            if (display == null) return DisplayPanel()
            val largest = display.supportedModes.maxByOrNull { it.physicalWidth.toLong() * it.physicalHeight }
            val current = display.mode
            return DisplayPanel(largest?.physicalWidth ?: 0, largest?.physicalHeight ?: 0, current.physicalWidth, current.physicalHeight)
        }

        @Volatile
        private var booted: DisplayPanel? = null

        /** The default display as this process first read it, at startup: the first-run pick and the migration read it. */
        fun atBoot(context: Context): DisplayPanel =
            booted ?: of(context.getSystemService(DisplayManager::class.java)?.getDisplay(Display.DEFAULT_DISPLAY)).also { booted = it }
    }
}
