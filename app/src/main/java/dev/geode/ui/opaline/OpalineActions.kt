package dev.geode.ui.opaline

import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay

/** How much presence an action has: a bright gel body, a mineral body, or no body at all. */
enum class OpalineEmphasis { Primary, Secondary, Quiet }

/**
 * A01/A05 gel capsule with native button semantics. [selected] marks an action that is currently
 * on (a live source, an active mode); its body turns to charged gel so the state is visible
 * without any colour-only cue in the label.
 */
@Composable
fun OpalineButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    enabled: Boolean = true,
    selected: Boolean = false,
    emphasis: OpalineEmphasis = OpalineEmphasis.Secondary,
) {
    val palette = Opaline.palette
    val interaction = remember { MutableInteractionSource() }
    val contact = rememberOpalineContact(interaction, enabled)
    val bright = emphasis == OpalineEmphasis.Primary || selected
    val body =
        when {
            emphasis == OpalineEmphasis.Quiet && !selected -> null
            bright -> OpalineBody.Action
            else -> OpalineBody.Action.copy(material = OpalineMaterial.STONE, dome = 2.dp)
        }
    val foreground =
        when {
            bright -> palette.onGel
            emphasis == OpalineEmphasis.Quiet -> palette.accent
            else -> palette.text
        }
    val surface =
        if (body != null) {
            rememberOpalineSurface(body, contact, selected = selected, enabled = enabled)
        } else {
            rememberQuietFocus(contact)
        }
    Row(
        modifier
            .defaultMinSize(minWidth = 48.dp, minHeight = 48.dp)
            .then(surface)
            .semantics { if (selected) this.selected = true }
            .clickable(interactionSource = interaction, indication = null, enabled = enabled, role = Role.Button, onClick = onClick)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .padding(buttonPadding(body)),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally),
    ) {
        if (icon != null) Icon(icon, null, Modifier.size(20.dp), tint = foreground)
        Text(text, style = MaterialTheme.typography.labelLarge, color = foreground, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

private fun buttonPadding(body: OpalineBody?): PaddingValues {
    val wall = body?.wall ?: 0.dp
    return PaddingValues(start = 20.dp, end = 20.dp, top = 12.dp, bottom = 12.dp + wall)
}

/**
 * A03 round lens holding one icon. Secondary lenses are mineral; [selected] charges them to gel.
 * Quiet lenses have no body and suit repeated per-row actions, keeping lists calm.
 */
@Composable
fun OpalineIconButton(
    icon: ImageVector,
    contentDescription: String?,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    selected: Boolean = false,
    emphasis: OpalineEmphasis = OpalineEmphasis.Secondary,
    size: Dp = 48.dp,
) {
    val palette = Opaline.palette
    val interaction = remember { MutableInteractionSource() }
    val contact = rememberOpalineContact(interaction, enabled)
    val bright = emphasis == OpalineEmphasis.Primary || selected
    val body =
        when {
            emphasis == OpalineEmphasis.Quiet && !selected -> null
            bright -> OpalineBody.Lens
            else -> OpalineBody.Lens.copy(material = OpalineMaterial.STONE, dome = 4.dp)
        }
    val tint =
        when {
            bright -> palette.onGel
            else -> palette.text
        }
    val surface =
        if (body != null) {
            rememberOpalineSurface(body, contact, selected = selected, enabled = enabled)
        } else {
            rememberQuietFocus(contact)
        }
    Box(
        modifier
            .size(size)
            .then(surface)
            .semantics { if (selected) this.selected = true }
            .clickable(interactionSource = interaction, indication = null, enabled = enabled, role = Role.Button, onClick = onClick)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .padding(bottom = body?.wall ?: 0.dp),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription, Modifier.size(size * ICON_FRACTION), tint = tint)
    }
}

/**
 * A22 soft puck: the one primary transport action. While [flowing] the interior's flow field
 * advances slowly (20 updates a second), and it stops entirely when paused or with reduced
 * motion, so a resting screen does no rendering work.
 */
@Composable
fun OpalinePuck(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    flowing: Boolean = false,
    size: Dp = 92.dp,
) {
    val palette = Opaline.palette
    val interaction = remember { MutableInteractionSource() }
    val contact = rememberOpalineContact(interaction, enabled)
    val reduced = Opaline.reducedMotion
    var time by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(flowing, reduced) {
        if (!flowing || reduced) return@LaunchedEffect
        while (true) {
            delay(FLOW_TICK_MS)
            time += FLOW_TICK_MS / 1000f
        }
    }
    val surface =
        rememberOpalineSurface(
            OpalineBody.Puck,
            contact,
            selected = flowing,
            enabled = enabled,
            flowTime = { time },
        )
    Box(
        modifier
            .size(size)
            .then(surface)
            .clickable(interactionSource = interaction, indication = null, enabled = enabled, role = Role.Button, onClick = onClick)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .padding(bottom = OpalineBody.Puck.wall),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription, Modifier.size(size * 0.4f), tint = palette.onGel)
    }
}

/**
 * B12-style selector bead for one choice among several. Use inside [OpalineChipRow] or another
 * `selectableGroup` so assistive technology announces the group and the selected member.
 */
@Composable
fun OpalineChip(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    icon: ImageVector? = null,
    role: Role = Role.RadioButton,
) {
    val palette = Opaline.palette
    val interaction = remember { MutableInteractionSource() }
    val contact = rememberOpalineContact(interaction, enabled)
    val body =
        if (selected) {
            OpalineBody.Action.copy(height = 5.dp, wall = 3.dp, dome = 2.dp)
        } else {
            OpalineBody.Action.copy(material = OpalineMaterial.STONE, height = 4.dp, wall = 2.dp, dome = 1.dp, receiver = 0.35f)
        }
    val foreground = if (selected) palette.onGel else palette.text
    Row(
        modifier
            .defaultMinSize(minWidth = 48.dp, minHeight = 44.dp)
            .then(rememberOpalineSurface(body, contact, selected = selected, enabled = enabled))
            .selectable(selected = selected, interactionSource = interaction, indication = null, enabled = enabled, role = role, onClick = onClick)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .padding(start = 16.dp, end = 16.dp, top = 10.dp, bottom = 10.dp + body.wall),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally),
    ) {
        if (icon != null) Icon(icon, null, Modifier.size(18.dp), tint = foreground)
        Text(label, style = MaterialTheme.typography.labelMedium, color = foreground, maxLines = 1)
    }
}

/** A scrollable group of [OpalineChip]s with exactly one selected. */
@Composable
fun OpalineChipRow(
    labels: List<String>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    role: Role = Role.RadioButton,
    contentPadding: PaddingValues = PaddingValues(0.dp),
) {
    Row(
        modifier
            .horizontalScroll(rememberScrollState())
            .selectableGroup()
            .padding(contentPadding)
            .padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        labels.forEachIndexed { index, label ->
            OpalineChip(label, index == selectedIndex, { onSelect(index) }, enabled = enabled, role = role)
        }
    }
}

/** Page-level tabs: the same selector beads, announced as tabs. */
@Composable
fun OpalineTabs(
    titles: List<String>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
    contentPadding: PaddingValues = PaddingValues(0.dp),
) = OpalineChipRow(titles, selectedIndex, onSelect, modifier, role = Role.Tab, contentPadding = contentPadding)

/** A bodiless action still shows host focus as a ring and darkens briefly under contact. */
@Composable
private fun rememberQuietFocus(contact: OpalineContact): Modifier {
    val palette = Opaline.palette
    return Modifier.drawQuietFocus(contact, palette.accent, Color.White)
}

internal const val DISABLED_ALPHA = 0.45f
private const val ICON_FRACTION = 0.46f
private const val FLOW_TICK_MS = 50L
