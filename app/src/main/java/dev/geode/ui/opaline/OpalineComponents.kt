package dev.geode.ui.opaline

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.sizeIn
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R

/** Unscrolling page frame; the caller owns its lazy list or scroll state. */
@Composable
fun OpalinePage(
    title: String,
    subtitle: String = "",
    onBack: (() -> Unit)? = null,
    actions: @Composable RowScope.() -> Unit = {},
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(Modifier.fillMaxSize()) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 18.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            if (onBack != null) {
                OpalineIconButton(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.opaline_back), onBack)
            }
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(title, style = MaterialTheme.typography.headlineLarge, color = OpalineColors.text)
                if (subtitle.isNotBlank()) {
                    Text(subtitle, style = MaterialTheme.typography.bodyMedium, color = OpalineColors.muted)
                }
            }
            actions()
        }
        content()
    }
}

/** A quiet content front with a real C03 structural shell registered beneath it. */
@Composable
fun OpalinePanel(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    val sceneReady = opalineReady()
    Column(
        modifier
            .fillMaxWidth()
            .opalinePart("C03")
            .padding(5.dp)
            .clip(RoundedCornerShape(24.dp))
            .background(
                Brush.verticalGradient(
                    if (sceneReady) {
                        listOf(OpalineColors.surface.copy(alpha = 0.32f), OpalineColors.deep.copy(alpha = 0.44f))
                    } else {
                        listOf(OpalineColors.surface.copy(alpha = 0.94f), OpalineColors.deep.copy(alpha = 0.96f))
                    },
                ),
            ).padding(18.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        content = content,
    )
}

/** Pressable gel A01 with native button semantics and a stable text frame. */
@Composable
fun OpalineButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    enabled: Boolean = true,
    selected: Boolean = false,
) {
    val sceneReady = opalineReady()
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val shape = RoundedCornerShape(50)
    Row(
        modifier
            .defaultMinSize(minHeight = 48.dp)
            .opalinePart("A01", selected = selected, enabled = enabled)
            .alpha(if (enabled) 1f else 0.45f)
            .clip(shape)
            .background(
                if (sceneReady) {
                    (if (selected) OpalineColors.accent else OpalineColors.deep).copy(alpha = if (selected) 0.22f else 0.26f)
                } else {
                    (if (selected) OpalineColors.accent else OpalineColors.gel).copy(alpha = if (selected) 0.88f else 0.82f)
                },
            ).then(if (focused) Modifier.border(2.dp, OpalineColors.pearl, shape) else Modifier)
            .clickable(interactionSource = interaction, indication = null, enabled = enabled, role = Role.Button, onClick = onClick)
            .padding(horizontal = 18.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally),
    ) {
        val foreground = if (sceneReady) OpalineColors.pearl else OpalineColors.ink
        if (icon != null) Icon(icon, null, Modifier.size(20.dp), tint = foreground)
        Text(text, style = MaterialTheme.typography.labelLarge, color = foreground, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
fun OpalineIconButton(
    icon: ImageVector,
    description: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val sceneReady = opalineReady()
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    Box(
        modifier
            .sizeIn(minWidth = 48.dp, minHeight = 48.dp)
            .opalinePart("A03", enabled = enabled)
            .alpha(if (enabled) 1f else 0.45f)
            .clip(CircleShape)
            .background(OpalineColors.surface.copy(alpha = if (sceneReady) 0.18f else 0.58f))
            .then(if (focused) Modifier.border(2.dp, OpalineColors.accent, CircleShape) else Modifier)
            .clickable(interactionSource = interaction, indication = null, enabled = enabled, role = Role.Button, onClick = onClick)
            .padding(12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, description, Modifier.size(24.dp), tint = OpalineColors.text)
    }
}

@Composable
fun OpalineRow(
    title: String,
    subtitle: String = "",
    onClick: (() -> Unit)? = null,
    modifier: Modifier = Modifier,
    leading: @Composable () -> Unit = {},
    trailing: @Composable () -> Unit = {},
) {
    val sceneReady = opalineReady()
    Row(
        modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = 64.dp)
            .opalinePart("C02")
            .padding(5.dp)
            .clip(RoundedCornerShape(18.dp))
            .background(OpalineColors.deep.copy(alpha = if (sceneReady) 0.38f else 0.8f))
            .then(if (onClick != null) Modifier.clickable(role = Role.Button, onClick = onClick) else Modifier)
            .padding(horizontal = 14.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        leading()
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(
                title,
                style = MaterialTheme.typography.titleSmall,
                color = OpalineColors.text,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            if (subtitle.isNotBlank()) {
                Text(
                    subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = OpalineColors.muted,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        trailing()
    }
}

@Composable
fun OpalineSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    range: ClosedFloatingPointRange<Float> = 0f..1f,
    enabled: Boolean = true,
    modifier: Modifier = Modifier,
) {
    val sceneReady = opalineReady()
    val safe = if (value.isFinite()) value.coerceIn(range) else range.start
    val length = range.endInclusive - range.start
    Slider(
        value = safe,
        onValueChange = onValueChange,
        modifier =
            modifier
                .defaultMinSize(minHeight = 48.dp)
                .opalinePart("B01", value = if (length > 0) (safe - range.start) / length else 0f, enabled = enabled),
        valueRange = range,
        enabled = enabled,
        colors =
            SliderDefaults.colors(
                thumbColor = OpalineColors.pearl.copy(alpha = if (sceneReady) 0.14f else 1f),
                activeTrackColor = OpalineColors.accent.copy(alpha = if (sceneReady) 0.16f else 0.8f),
                inactiveTrackColor = OpalineColors.surface.copy(alpha = if (sceneReady) 0.1f else 0.55f),
            ),
    )
}

@Composable
fun OpalineToggle(
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val sceneReady = opalineReady()
    Box(
        modifier
            .width(64.dp)
            .height(48.dp)
            .opalinePart("B09", value = if (checked) 1f else 0f, selected = checked, enabled = enabled)
            .alpha(if (enabled) 1f else 0.45f)
            .clip(RoundedCornerShape(50))
            .toggleable(value = checked, enabled = enabled, role = Role.Switch, onValueChange = onCheckedChange)
            .padding(8.dp)
            .background(OpalineColors.deep.copy(alpha = if (sceneReady) 0.14f else 0.75f), RoundedCornerShape(50)),
        contentAlignment = if (checked) Alignment.CenterEnd else Alignment.CenterStart,
    ) {
        Box(
            Modifier.size(28.dp).background(
                (if (checked) OpalineColors.accent else OpalineColors.muted).copy(alpha = if (sceneReady) 0.12f else 1f),
                CircleShape,
            ),
        )
    }
}

@Composable
fun OpalineEmptyState(
    title: String,
    message: String,
    action: @Composable () -> Unit = {},
) {
    Column(
        Modifier.fillMaxWidth().padding(28.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Spacer(Modifier.height(8.dp))
        Text(title, style = MaterialTheme.typography.titleLarge, color = OpalineColors.text)
        Text(message, style = MaterialTheme.typography.bodyMedium, color = OpalineColors.muted)
        action()
    }
}
