@file:Suppress("MatchingDeclarationName")

package dev.geode.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.BlurEffect
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.TileMode
import androidx.compose.ui.graphics.addOutline
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.layer.GraphicsLayer
import androidx.compose.ui.graphics.layer.drawLayer
import androidx.compose.ui.graphics.rememberGraphicsLayer
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInWindow
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * The glass blur token (owner's LOOK REVISION: "Ui can have glow, matte glass and blur"): what lies
 * under a floating glass surface, blurred.
 *
 * WHY a recorded backdrop and not a blur of the window: Compose has no "blur what is behind me".
 * [glassBackdrop] records the content it wraps into a [GraphicsLayer] (a RenderNode) as it draws it
 * anyway; [glassBlur] draws that layer again, moved to the glass's own position, into a second layer
 * the size of the glass with a [BlurEffect] on it. So the blur costs one pass over the glass's area,
 * never the whole screen, and it is not animated: the blurred layer only refers to the backdrop's
 * RenderNode, so the backdrop changing does not recompose or re-record the glass.
 *
 * What it cannot reach: the live visual. VisualizerView is a GLSurfaceView, a surface of its own that
 * SurfaceFlinger composites under the window, so it is not in any RenderNode a view can sample. A
 * blurred visual under the sheet has to come from the renderer itself (a native pass over the sheet's
 * rectangle, ADR 0003), which keeps the visual's frame rate the renderer's business. Never wrap the
 * VisualizerView in [glassBackdrop]: its hole would be recorded, and it would be drawn twice.
 *
 * The one rule: the glass must not sit inside its own backdrop, or the layer would draw itself.
 * Wrap the content beside and under the glass ([glassBackdrop]) and provide it through
 * [LocalGlassBackdrop]; with no backdrop provided, [glassBlur] does nothing.
 */
@Stable
class GlassBackdrop internal constructor() {
    internal var layer: GraphicsLayer? by mutableStateOf(null)
    internal var origin: Offset by mutableStateOf(Offset.Zero)
}

/** The backdrop the glass surfaces in this subtree blur, or null for none (the matte fill alone). */
val LocalGlassBackdrop = staticCompositionLocalOf<GlassBackdrop?> { null }

@Composable
fun rememberGlassBackdrop(): GlassBackdrop = remember { GlassBackdrop() }

/** Records what this draws, for the glass above it ([glassBlur]); it still draws it once, as usual. */
@Composable
fun Modifier.glassBackdrop(backdrop: GlassBackdrop): Modifier {
    val layer = rememberGraphicsLayer()
    DisposableEffect(backdrop, layer) {
        backdrop.layer = layer
        onDispose { if (backdrop.layer === layer) backdrop.layer = null }
    }
    return this
        .onGloballyPositioned { backdrop.origin = it.positionInWindow() }
        .drawWithContent {
            layer.record { this@drawWithContent.drawContent() }
            drawLayer(layer)
        }
}

/**
 * The blurred backdrop inside [shape], drawn under what follows in the chain. Put it before
 * [floatingGlass] (`Modifier.glassBlur(shape).floatingGlass(shape)`), so the matte fill tints the
 * blur and not the other way round.
 */
@Composable
fun Modifier.glassBlur(
    shape: Shape = RoundedCornerShape(GeodeRadius.Floating),
    radius: Dp = GlassBlurRadius,
): Modifier {
    val backdrop = LocalGlassBackdrop.current
    if (backdrop == null) return this
    val blurred = rememberGraphicsLayer()
    val radiusPx = with(LocalDensity.current) { radius.toPx() }
    SideEffect { blurred.renderEffect = BlurEffect(radiusPx, radiusPx, TileMode.Clamp) }
    // State read in the draw pass only: a move redraws the blur, it never recomposes the glass.
    val here = remember { mutableStateOf(Offset.Zero) }
    return this
        .onGloballyPositioned { here.value = it.positionInWindow() }
        .drawWithCache {
            // Clipped here, not with Modifier.clip: that would clip the glass's shadow after it too.
            val outline = Path().apply { addOutline(shape.createOutline(size, layoutDirection, this@drawWithCache)) }
            onDrawBehind {
                val source = backdrop.layer ?: return@onDrawBehind
                val shift = here.value - backdrop.origin
                blurred.record { translate(-shift.x, -shift.y) { drawLayer(source) } }
                clipPath(outline) { drawLayer(blurred) }
            }
        }
}

/** Enough to turn shapes into colour behind the glass while the matte fill keeps text readable. */
val GlassBlurRadius = 24.dp
