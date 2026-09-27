package dev.geode.ui.opaline

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties

/**
 * A modal C03 slab with a title, optional body and actions. The dialog window owns focus and
 * back; long bodies scroll between the fixed title and actions.
 */
@Composable
fun OpalineDialog(
    onDismissRequest: () -> Unit,
    title: String,
    modifier: Modifier = Modifier,
    text: String? = null,
    confirm: @Composable RowScope.() -> Unit,
    dismiss: (@Composable RowScope.() -> Unit)? = null,
    content: (@Composable ColumnScope.() -> Unit)? = null,
) {
    val palette = Opaline.palette
    Dialog(onDismissRequest, DialogProperties(usePlatformDefaultWidth = false)) {
        Box(Modifier.padding(24.dp), contentAlignment = Alignment.Center) {
            OpalinePanel(modifier.widthIn(min = 280.dp, max = 460.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                Text(title, Modifier.semantics { heading() }, style = MaterialTheme.typography.headlineSmall, color = palette.text)
                if (text != null || content != null) {
                    Column(
                        Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState()),
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        if (text != null) Text(text, style = MaterialTheme.typography.bodyMedium, color = palette.textMuted)
                        content?.invoke(this)
                    }
                }
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    dismiss?.invoke(this)
                    confirm()
                }
            }
        }
    }
}

/**
 * A menu on a floating slab. Positioning, dismissal and focus come from the platform dropdown;
 * the slab replaces its surface.
 */
@Composable
fun OpalineMenu(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    DropdownMenu(
        expanded = expanded,
        onDismissRequest = onDismissRequest,
        modifier = modifier.widthIn(min = 200.dp).then(rememberOpalineSurface(MenuBody)),
        shape = RoundedCornerShape(MenuBody.radius),
        containerColor = Color.Transparent,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
        content = content,
    )
}

@Composable
fun OpalineMenuItem(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    enabled: Boolean = true,
    selected: Boolean = false,
) {
    val palette = Opaline.palette
    val interaction = remember { MutableInteractionSource() }
    val contact = rememberOpalineContact(interaction, enabled)
    Row(
        modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = 48.dp)
            .drawQuietFocus(contact, palette.accent, Color.White)
            .semantics { if (selected) this.selected = true }
            .clickable(interaction, null, enabled = enabled, role = Role.Button, onClick = onClick)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .padding(horizontal = 18.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        if (icon != null) Icon(icon, null, Modifier.size(20.dp), tint = if (selected) palette.accent else palette.textMuted)
        Text(
            text,
            style = MaterialTheme.typography.bodyLarge,
            color = if (selected) palette.accent else palette.text,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

private val MenuBody = OpalineBody.Panel.copy(radius = 24.dp, wall = 0.dp, receiver = 0f)
