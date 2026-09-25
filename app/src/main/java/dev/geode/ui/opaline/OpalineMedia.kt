package dev.geode.ui.opaline

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

/**
 * C20 circular frame: a thick blue-gel annulus holding artwork on its stable inner ring. While
 * [active] (music is actually playing) the gel's interior flows slowly; otherwise it is still.
 */
@Composable
fun OpalineMediaRing(
    active: Boolean,
    modifier: Modifier = Modifier,
    ringWidth: Dp = 22.dp,
    onClick: (() -> Unit)? = null,
    onClickLabel: String? = null,
    content: @Composable BoxScope.() -> Unit,
) {
    val interaction = remember { MutableInteractionSource() }
    val contact = rememberOpalineContact(interaction, onClick != null)
    val reduced = Opaline.reducedMotion
    var time by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(active, reduced) {
        if (!active || reduced) return@LaunchedEffect
        while (true) {
            delay(RING_TICK_MS)
            time += RING_TICK_MS / 1000f
        }
    }
    Box(
        modifier
            .then(rememberOpalineSurface(OpalineBody.Ring, contact, selected = active, flowTime = { time }))
            .then(
                if (onClick != null) {
                    Modifier.clickable(interaction, null, onClickLabel = onClickLabel, role = Role.Button, onClick = onClick)
                } else {
                    Modifier
                },
            ).padding(start = ringWidth, end = ringWidth, top = ringWidth, bottom = ringWidth + OpalineBody.Ring.wall)
            .clip(CircleShape),
        contentAlignment = Alignment.Center,
        content = content,
    )
}

/**
 * The shared environment behind every section: a themed abstract liquid space baked off the main
 * thread at a fraction of screen resolution (it is soft by design), faded in over a gradient of
 * the same colours so there is never a blank frame. [dim] darkens it for readability.
 */
@Composable
fun OpalineBackdrop(
    modifier: Modifier = Modifier,
    dim: Float = 0f,
) {
    val palette = Opaline.palette
    var size by remember { mutableStateOf(IntSize.Zero) }
    val bitmap by produceState<ImageBitmap?>(null, palette.theme, size) {
        if (size.width <= 0 || size.height <= 0) return@produceState
        val width = (size.width / BACKDROP_DOWNSCALE).toInt().coerceAtLeast(2)
        val height = (size.height / BACKDROP_DOWNSCALE).toInt().coerceAtLeast(2)
        value = withContext(Dispatchers.Default) { OpalineTextures.backdrop(palette, width, height).asImageBitmap() }
    }
    val reveal by animateFloatAsState(if (bitmap != null) 1f else 0f, tween(if (Opaline.reducedMotion) 0 else REVEAL_MS), label = "backdrop")
    Canvas(modifier.fillMaxSize().onSizeChanged { size = it }) {
        drawRect(Brush.verticalGradient(listOf(palette.environment, palette.environmentLow)))
        bitmap?.let { image ->
            drawImage(
                image,
                srcOffset = IntOffset.Zero,
                srcSize = IntSize(image.width, image.height),
                dstSize = IntSize(this.size.width.toInt(), this.size.height.toInt()),
                alpha = reveal,
            )
        }
        if (dim > 0f) drawRect(Color.Black.copy(alpha = dim.coerceIn(0f, 1f) * MAX_DIM), topLeft = Offset.Zero)
    }
}

private const val RING_TICK_MS = 50L
private const val BACKDROP_DOWNSCALE = 2.5f
private const val REVEAL_MS = 450
private const val MAX_DIM = 0.6f
