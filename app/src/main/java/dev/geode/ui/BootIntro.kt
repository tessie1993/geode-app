package dev.geode.ui

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.AnimationVector1D
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.geode.R
import dev.geode.ui.glass.GlassPalette
import dev.geode.ui.glass.LocalGlass
import dev.geode.ui.glass.LocalWaterField
import dev.geode.ui.glass.WaterField
import kotlin.math.max
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

private const val DROP_FALL_MS = 650
private const val DROP_LAND_FRACTION = 0.42f
private const val DROP_TAP_STRENGTH = 10f
private const val RING_COUNT = 3
private const val RING_STAGGER_MS = 160
private const val RING_TRAVEL_MS = 900
private const val TEXT_IN_MS = 450
private const val FADE_START_MS = 1100L
private const val FADE_OUT_MS = 300
private const val REDUCED_MOTION_HOLD_MS = 900L

/**
 * The boot intro: a glass drop falls onto the shared water surface, sends ripple rings out from
 * where it lands, and the wordmark fades in as the surface settles. Tapping anywhere skips
 * straight to [onDone]. Under reduced motion the drop and rings are skipped and the wordmark
 * appears directly, matching [dev.geode.ui.GuiPrefs.reducedMotion].
 */
@Composable
fun BootIntro(onDone: () -> Unit) {
    val reducedMotion = LocalGlass.current.reducedMotion
    val field = LocalWaterField.current
    val overlayAlpha = remember { Animatable(1f) }
    val dropFall = remember { Animatable(0f) }
    val dropAlpha = remember { Animatable(1f) }
    val textAlpha = remember { Animatable(0f) }
    val textScale = remember { Animatable(0.7f) }
    val rings = remember { List(RING_COUNT) { Animatable(0f) } }

    LaunchedEffect(reducedMotion) {
        if (reducedMotion) {
            // No drop, no field impulse, no rings: reduced motion means the shared water surface
            // (and anything floating on it) stays still while this overlay is up.
            textAlpha.snapTo(1f)
            textScale.snapTo(1f)
            dropAlpha.snapTo(0f)
            delay(REDUCED_MOTION_HOLD_MS)
            overlayAlpha.animateTo(0f, tween(FADE_OUT_MS))
            onDone()
            return@LaunchedEffect
        }
        launch {
            dropFall.animateTo(1f, tween(DROP_FALL_MS, easing = FastOutSlowInEasing))
            field?.tapLanding()
            dropAlpha.animateTo(0f, tween(180))
        }
        launch {
            delay(DROP_FALL_MS.toLong())
            textAlpha.animateTo(1f, tween(TEXT_IN_MS, easing = LinearOutSlowInEasing))
        }
        launch {
            delay(DROP_FALL_MS.toLong())
            textScale.animateTo(1f, tween(TEXT_IN_MS + 100, easing = FastOutSlowInEasing))
        }
        rings.forEachIndexed { i, ring ->
            launch {
                delay(DROP_FALL_MS + i * RING_STAGGER_MS.toLong())
                ring.animateTo(1f, tween(RING_TRAVEL_MS, easing = FastOutSlowInEasing))
            }
        }
        delay(DROP_FALL_MS + FADE_START_MS)
        overlayAlpha.animateTo(0f, tween(FADE_OUT_MS))
        onDone()
    }

    Box(
        Modifier
            .fillMaxSize()
            .graphicsLayer { alpha = overlayAlpha.value }
            .background(GlassPalette.base.copy(alpha = 0.3f))
            .pointerInput(Unit) { detectTapGestures { onDone() } },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val landing = Offset(size.width / 2f, size.height * DROP_LAND_FRACTION)
            drawFallingDrop(landing, dropFall.value, dropAlpha.value)
            drawLandingRipples(landing, rings)
        }
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            modifier =
                Modifier.graphicsLayer {
                    alpha = textAlpha.value
                    scaleX = textScale.value
                    scaleY = textScale.value
                },
        ) {
            Text(
                stringResource(R.string.boot_wordmark),
                color = GlassPalette.textPrimary,
                style =
                    MaterialTheme.typography.headlineLarge.copy(
                        shadow = Shadow(color = GlassPalette.mint, blurRadius = 36f),
                    ),
            )
            Text(
                stringResource(R.string.boot_tagline),
                color = GlassPalette.textSecondary,
                style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 3.5.sp),
                modifier = Modifier.padding(top = 10.dp),
            )
        }
    }
}

/** A best-effort impulse into the shared field where the drop lands; a no-op before the field
 * has been laid out. */
private fun WaterField.tapLanding() {
    if (canvasWidth > 1f) tap(canvasWidth / 2f, canvasHeight * DROP_LAND_FRACTION, DROP_TAP_STRENGTH)
}

private fun DrawScope.drawFallingDrop(
    landing: Offset,
    fallProgress: Float,
    alpha: Float,
) {
    if (alpha < 0.01f) return
    val startY = -size.height * 0.12f
    val y = startY + (landing.y - startY) * fallProgress
    val center = Offset(landing.x, y)
    val radius = 16.dp.toPx()
    drawCircle(
        brush =
            Brush.radialGradient(
                listOf(Color.White.copy(alpha = 0.55f * alpha), GlassPalette.mint.copy(alpha = 0.22f * alpha)),
                center = center,
                radius = radius,
            ),
        radius = radius,
        center = center,
    )
    drawCircle(
        GlassPalette.glassRim.copy(alpha = 0.6f * alpha),
        radius = radius,
        center = center,
        style = Stroke(width = 1.5.dp.toPx()),
    )
    drawCircle(
        Color.White.copy(alpha = 0.5f * alpha),
        radius = radius * 0.3f,
        center = center + Offset(-radius * 0.3f, -radius * 0.3f),
    )
}

private fun DrawScope.drawLandingRipples(
    landing: Offset,
    rings: List<Animatable<Float, AnimationVector1D>>,
) {
    val maxRadius = max(size.width, size.height) * 0.7f
    val ringColors = listOf(GlassPalette.mint, GlassPalette.lavender, GlassPalette.peach)
    rings.forEachIndexed { i, ring ->
        val p = ring.value
        if (p <= 0f || p >= 1f) return@forEachIndexed
        val radius = max(1f, maxRadius * p)
        val fade = 1f - p
        val color = ringColors[i % ringColors.size]
        drawCircle(color.copy(alpha = fade * 0.28f), radius = radius, center = landing, style = Stroke(width = 2.dp.toPx()))
        drawCircle(
            GlassPalette.glassRim.copy(alpha = fade * 0.4f),
            radius = radius,
            center = landing,
            style = Stroke(width = 1.dp.toPx()),
        )
    }
}
