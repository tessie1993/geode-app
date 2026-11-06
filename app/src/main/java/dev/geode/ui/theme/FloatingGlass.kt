package dev.geode.ui.theme

import androidx.compose.foundation.background
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.Dp

/**
 * Floating black matte glass: the surface of the sheet, mini player, quick settings, pop-ups and
 * the tab bar pill.
 *
 * A soft real shadow (the platform's ambient and key shadows, drawn by the render thread) lifts
 * the panel off the visual; then black at [fillAlpha] lets the visual show through. No border,
 * no sheen, no glow: the shadow and the darkening alone make the edge, which is what reads as
 * matte glass rather than a lit crystal. The caller insets it from the screen edges
 * ([GeodeSpacing.EdgeInset]) and from the system bars.
 *
 * [fillAlpha] is the user's sheet opacity where there is one, clamped to the range the glass
 * stays readable in: [GeodeGlass.MATTE_FILL_MIN] is the lowest alpha at which the theme's text
 * still clears 4.5:1 over a pure white frame ([GeodeGlass.WorstBackdrop]), so no setting can draw
 * glass the text fails on.
 */
fun Modifier.floatingGlass(
    shape: Shape = RoundedCornerShape(GeodeRadius.Floating),
    fillAlpha: Float = GeodeGlass.MATTE_FILL,
    elevation: Dp = GeodeElevation.Floating,
): Modifier =
    this
        .shadow(
            elevation = elevation,
            shape = shape,
            clip = false,
            ambientColor = GeodeColors.Shadow.copy(alpha = GeodeElevation.AMBIENT_ALPHA),
            spotColor = GeodeColors.Shadow.copy(alpha = GeodeElevation.SPOT_ALPHA),
        ).clip(shape)
        .background(GeodeColors.Canvas.copy(alpha = fillAlpha.coerceIn(GeodeGlass.MATTE_FILL_MIN, GeodeGlass.MATTE_FILL_MAX)))
