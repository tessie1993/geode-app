package dev.geode.ui

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.geode.R
import dev.geode.ui.theme.LocalReducedMotion
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.max

private const val RING_COUNT = 3
private const val RING_STAGGER_MS = 160
private const val RING_TRAVEL_MS = 900
private const val TEXT_IN_MS = 450
private const val FADE_START_MS = 1100L
private const val FADE_OUT_MS = 300

/**
 * The system splash's icon box when the icon has no background of its own: 288dp, with the mark
 * inside its 192dp circle. Drawing the same drawable at the same size, centred on the same colour,
 * makes BootIntro's first frame the splash's last, so the two read as one intro, not two.
 */
private val SPLASH_ICON_SIZE = 288.dp

/** The iris, where the rings start: r=15 of the mark's 108-unit viewport, at [SPLASH_ICON_SIZE]. */
private val IRIS_RADIUS = SPLASH_ICON_SIZE * (15f / 108f)

@Composable
fun BootIntro(onDone: () -> Unit) {
    val overlayAlpha = remember { Animatable(1f) }
    val textAlpha = remember { Animatable(0f) }
    val textScale = remember { Animatable(0.7f) }
    val rings = remember { List(RING_COUNT) { Animatable(0f) } }

    val reducedMotion = LocalReducedMotion.current
    LaunchedEffect(Unit) {
        // Reduced motion means no intro at all, not a slower one.
        if (reducedMotion) {
            onDone()
            return@LaunchedEffect
        }
        launch { textAlpha.animateTo(1f, tween(TEXT_IN_MS, easing = LinearOutSlowInEasing)) }
        launch { textScale.animateTo(1f, tween(TEXT_IN_MS + 100, easing = FastOutSlowInEasing)) }
        rings.forEachIndexed { i, ring ->
            launch {
                delay(i * RING_STAGGER_MS.toLong())
                ring.animateTo(1f, tween(RING_TRAVEL_MS, easing = FastOutSlowInEasing))
            }
        }
        delay(FADE_START_MS)
        overlayAlpha.animateTo(0f, tween(FADE_OUT_MS))
        onDone()
    }

    val primary = MaterialTheme.colorScheme.primary
    val onBackground = MaterialTheme.colorScheme.onBackground
    val glint = lerp(primary, onBackground, 0.72f)
    val wordmark = LocalFontColor.current ?: glint
    Box(
        Modifier
            .fillMaxSize()
            .graphicsLayer { alpha = overlayAlpha.value }
            .background(colorResource(R.color.brand_night))
            .pointerInput(Unit) { detectTapGestures { onDone() } },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val startRadius = IRIS_RADIUS.toPx()
            val endRadius = max(size.width, size.height) * 0.72f
            val strokeWidth = 2.dp.toPx()
            rings.forEach { ring ->
                val p = ring.value
                if (p > 0f && p < 1f) {
                    val radius = startRadius + (endRadius - startRadius) * p
                    val fade = 1f - p
                    drawCircle(
                        color = primary.copy(alpha = fade * 0.12f),
                        radius = radius,
                        style = Stroke(width = strokeWidth * 7f),
                    )
                    drawCircle(
                        color = primary.copy(alpha = fade * 0.28f),
                        radius = radius,
                        style = Stroke(width = strokeWidth * 3f),
                    )
                    drawCircle(
                        color = glint.copy(alpha = fade * 0.5f),
                        radius = radius,
                        style = Stroke(width = strokeWidth),
                    )
                }
            }
        }
        Image(
            painterResource(R.drawable.ic_launcher_foreground),
            contentDescription = null,
            modifier = Modifier.size(SPLASH_ICON_SIZE),
        )
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            // Below the mark, whose lids reach 57dp under the centre at this size.
            modifier =
                Modifier
                    .offset(y = 112.dp)
                    .graphicsLayer {
                        alpha = textAlpha.value
                        scaleX = textScale.value
                        scaleY = textScale.value
                    },
        ) {
            Text(
                stringResource(R.string.boot_wordmark),
                color = wordmark,
                style = MaterialTheme.typography.displaySmall,
            )
            Text(
                stringResource(R.string.boot_tagline).uppercase(),
                color = primary.copy(alpha = 0.85f),
                style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 3.5.sp),
                modifier = Modifier.padding(top = 10.dp),
            )
        }
    }
}
