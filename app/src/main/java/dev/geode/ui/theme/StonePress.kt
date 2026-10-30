package dev.geode.ui.theme

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.interaction.InteractionSource
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalView

/** Whether the user asked for reduced motion (Look setting, or the system's "remove animations"). */
val LocalReducedMotion = staticCompositionLocalOf { false }

/** A light haptic tick and a small scale-down while pressed; no scale under reduced motion. */
@Composable
fun Modifier.stonePress(interaction: InteractionSource): Modifier {
    val reducedMotion = LocalReducedMotion.current
    val pressed by interaction.collectIsPressedAsState()
    val view = LocalView.current
    LaunchedEffect(pressed) {
        if (pressed) view.performStoneHaptic(StoneHapticCue.TAP)
    }
    // Kept as State and read inside graphicsLayer, so a press animates in the draw phase instead of
    // recomposing the button on every frame of the spring.
    val scale = animateFloatAsState(
        targetValue = if (pressed && !reducedMotion) GeodeMotion.PRESS_SCALE else 1f,
        animationSpec =
            if (pressed) {
                tween(GeodeMotion.PRESS)
            } else {
                spring(dampingRatio = 0.78f, stiffness = 380f)
            },
        label = "stone-press",
    )
    return graphicsLayer {
        scaleX = scale.value
        scaleY = scale.value
    }
}

@Composable
fun rememberStoneInteraction(): MutableInteractionSource = remember { MutableInteractionSource() }
