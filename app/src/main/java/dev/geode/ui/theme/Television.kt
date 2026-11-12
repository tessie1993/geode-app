package dev.geode.ui.theme

import android.content.Context
import android.content.pm.PackageManager
import androidx.compose.material.ripple.RippleAlpha
import androidx.compose.material3.RippleConfiguration
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/**
 * Whether the app runs on a TV (Android TV, Google TV): the one question the shell asks about the
 * device, answered the way "Handle TV hardware" recommends, by the leanback system feature rather
 * than by the UI mode or the screen size, so a phone on a big monitor stays a phone.
 */
fun Context.isTelevision(): Boolean = packageManager.hasSystemFeature(PackageManager.FEATURE_LEANBACK)

/**
 * [isTelevision], read once by `AppRoot` and handed down; false outside it (the paywall, the
 * wallpaper's options), where nothing reads it. Static: it never changes while the app runs.
 */
val LocalTelevision = staticCompositionLocalOf { false }

/**
 * The focus a remote moves, readable across a room: every M3 control and every plain `clickable`
 * draws its focused state as a ripple state layer, which at Material's own opacity (about 10 %)
 * is lost on a TV at three metres. On a TV the theme raises it to [FOCUSED_ALPHA] in the accent,
 * so whatever has focus glows in the visual's colour, the way the selected thing does
 * ([selectedGlow]). Phones keep Material's defaults: the theme provides this only on a TV.
 */
fun televisionRipple(accent: Color): RippleConfiguration =
    RippleConfiguration(
        color = accent,
        rippleAlpha =
            RippleAlpha(
                draggedAlpha = DRAGGED_ALPHA,
                focusedAlpha = FOCUSED_ALPHA,
                hoveredAlpha = HOVERED_ALPHA,
                pressedAlpha = PRESSED_ALPHA,
            ),
    )

private const val FOCUSED_ALPHA = 0.34f
private const val PRESSED_ALPHA = 0.24f
private const val DRAGGED_ALPHA = 0.16f
private const val HOVERED_ALPHA = 0.12f

/**
 * The part of a TV picture an older set may crop (overscan): the margins the TV layout guide asks
 * for, 5 % of 960 x 540 dp. The shell keeps its rail and screens inside them on a TV.
 */
object TelevisionSafeArea {
    val Horizontal = 48.dp
    val Vertical = 27.dp
}
