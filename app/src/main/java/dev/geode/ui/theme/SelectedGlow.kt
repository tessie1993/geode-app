package dev.geode.ui.theme

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.ClipOp
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.addOutline
import androidx.compose.ui.graphics.drawOutline
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.unit.dp

/**
 * A soft accent glow behind the selected thing: the one glow the look allows (owner's LOOK
 * REVISION). The selected tab, card or option gets a halo in the accent (the visual's colour),
 * so the choice reads at a glance over the live visual.
 *
 * Cheap on purpose: one shadow pass of [shape] drawn behind the content, no extra layer and no
 * animation per frame. It fades on and off through [LocalGeodeMotion]'s effects spec, and draws
 * nothing at all while it is off. The halo needs API 28 or later (a platform shadow layer on a
 * hardware canvas); on 26 and 27 only a faint fill behind the shape is drawn, which the
 * control's own background covers.
 */
fun Modifier.selectedGlow(
    selected: Boolean,
    shape: Shape = RoundedCornerShape(GeodeRadius.Floating),
): Modifier =
    composed {
        val motion = LocalGeodeMotion.current
        val accent = MaterialTheme.colorScheme.primary
        val strength by animateFloatAsState(
            targetValue = if (selected) 1f else 0f,
            animationSpec = motion.defaultEffectsSpec(),
            label = "selectedGlow",
        )
        val paint = remember { Paint() }
        drawBehind {
            if (strength <= 0f) return@drawBehind
            val glow = accent.copy(alpha = GLOW_ALPHA * strength)
            paint.color = glow.copy(alpha = glow.alpha * FILL_SHARE)
            paint.asFrameworkPaint().setShadowLayer(GLOW_RADIUS.toPx(), 0f, 0f, glow.toArgb())
            val outline = shape.createOutline(size, layoutDirection, this)
            // The shadow layer also paints the full glow under the shape. Over a large see-through
            // card (an open MilkDrop or Settings card) that read as a solid accent slab, so the
            // halo is kept outside the shape and the inside gets only the faint fill.
            clipPath(Path().apply { addOutline(outline) }, ClipOp.Difference) {
                drawIntoCanvas { it.drawOutline(outline, paint) }
            }
            drawOutline(outline, paint.color)
        }
    }

/** How strong the halo is at full: soft, never a neon edge. */
private const val GLOW_ALPHA = 0.45f

/** The share of the glow the shape's own fill gets, so the halo, not a tint, carries it. */
private const val FILL_SHARE = 0.25f

private val GLOW_RADIUS = 16.dp
