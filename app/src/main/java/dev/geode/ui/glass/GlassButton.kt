package dev.geode.ui.glass

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ProvideTextStyle
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/** A pill button: frosted glass body, optional leading icon, text content. */
@Composable
fun GlassButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    enabled: Boolean = true,
    selected: Boolean = false,
    tint: Color? = null,
) {
    val alpha = if (enabled) 1f else 0.5f
    Row(
        modifier
            .defaultMinSize(minHeight = 48.dp)
            .glassSurface(shape = GlassShapes.pill, tint = tint, selected = selected)
            .floatOnWater()
            .glassTouch(enabled = enabled, onClick = onClick)
            .padding(horizontal = 22.dp, vertical = 10.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val content = GlassPalette.textPrimary.copy(alpha = alpha)
        CompositionLocalProvider(LocalContentColor provides content) {
            if (icon != null) {
                Icon(icon, contentDescription = null, tint = content)
                Spacer(Modifier.size(8.dp))
            }
            ProvideTextStyle(MaterialTheme.typography.labelLarge) { Text(text) }
        }
    }
}

/** A circular icon button, e.g. the shortcut bubbles floating on the canvas in ref-05. */
@Composable
fun GlassBubbleButton(
    icon: ImageVector,
    contentDescription: String?,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    size: Dp = 52.dp,
    enabled: Boolean = true,
    tint: Color? = null,
    onLongPress: (() -> Unit)? = null,
) {
    Box(
        modifier
            .size(size)
            .glassSurface(shape = GlassShapes.bubble, tint = tint)
            .floatOnWater()
            .glassTouch(enabled = enabled, onClick = onClick, onLongPress = onLongPress),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            icon,
            contentDescription = contentDescription,
            tint = GlassPalette.textPrimary.copy(alpha = if (enabled) 1f else 0.5f),
            modifier = Modifier.size(size * 0.42f),
        )
    }
}

/** A rounded-square tile, for grid shortcuts / palette swatches. */
@Composable
fun GlassTile(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    tint: Color? = null,
    enabled: Boolean = true,
    contentPadding: PaddingValues = PaddingValues(12.dp),
    content: @Composable () -> Unit,
) {
    Box(
        modifier
            .glassSurface(shape = GlassShapes.tile, tint = tint)
            .floatOnWater()
            .glassTouch(enabled = enabled, onClick = onClick)
            .padding(contentPadding),
        contentAlignment = Alignment.Center,
    ) {
        content()
    }
}

/** The big 88 dp play/pause bubble from ref-05's transport row. */
@Composable
fun GlassPlayButton(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    Box(
        modifier
            .size(88.dp)
            .glassSurface(shape = GlassShapes.bubble, tint = GlassPalette.mint, glow = 0.4f)
            .floatOnWater(strength = 1.3f)
            .glassTouch(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            icon,
            contentDescription = contentDescription,
            tint = GlassPalette.textPrimary.copy(alpha = if (enabled) 1f else 0.5f),
            modifier = Modifier.size(36.dp),
        )
    }
}
