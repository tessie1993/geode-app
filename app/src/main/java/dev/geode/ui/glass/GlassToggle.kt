package dev.geode.ui.glass

import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

/** A glass pill with a pearl knob that slides, matching ref-03's "Toggle Switch". */
@Composable
fun GlassToggle(
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val trackWidth = 52.dp
    val trackHeight = 30.dp
    val knobSize = 24.dp
    val travel = trackWidth - knobSize - 6.dp
    val offset by animateDpAsState(if (checked) travel else 0.dp, label = "glassToggleKnob")
    Box(
        modifier
            .width(trackWidth)
            .height(trackHeight)
            .glassSurface(shape = GlassShapes.pill, tint = if (checked) GlassPalette.mint else null, selected = checked)
            .floatOnWater(strength = 0.3f)
            .glassTouch(enabled = enabled, onClick = { onCheckedChange(!checked) })
            .padding(3.dp),
        contentAlignment = Alignment.CenterStart,
    ) {
        Box(
            Modifier
                .offset(x = offset)
                .size(knobSize)
                .glassSurface(shape = GlassShapes.bubble, glow = if (checked) 0.3f else 0f),
        )
    }
}
