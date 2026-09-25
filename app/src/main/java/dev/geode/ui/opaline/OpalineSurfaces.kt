package dev.geode.ui.opaline

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ErrorOutline
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R

/** Shared layout measures: the page gutter and the rhythm between floating bodies. */
object OpalineSpacing {
    val gutter = 20.dp
    val item = 10.dp
    val section = 20.dp
    val panelPadding = 20.dp
}

/**
 * A page: its header (optional back lens, title, subtitle, actions) above [content]. The page
 * does not scroll; the caller owns its lazy list or scroll state and applies [OpalineSpacing.gutter].
 */
@Composable
fun OpalinePage(
    title: String,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    onBack: (() -> Unit)? = null,
    actions: @Composable RowScope.() -> Unit = {},
    content: @Composable ColumnScope.() -> Unit,
) {
    val palette = Opaline.palette
    Column(modifier.fillMaxSize()) {
        Row(
            Modifier
                .fillMaxWidth()
                .padding(start = if (onBack != null) 12.dp else OpalineSpacing.gutter, end = 12.dp, top = 12.dp, bottom = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            if (onBack != null) {
                OpalineIconButton(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.opaline_back), onBack)
            }
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    title,
                    Modifier.semantics { heading() },
                    style = if (onBack == null) MaterialTheme.typography.headlineLarge else MaterialTheme.typography.headlineMedium,
                    color = palette.text,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                if (!subtitle.isNullOrBlank()) {
                    Text(
                        subtitle,
                        style = MaterialTheme.typography.bodyMedium,
                        color = palette.textMuted,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically, content = actions)
        }
        content()
    }
}

/** C03 portrait slab: a quiet, deep panel grouping related content. */
@Composable
fun OpalinePanel(
    modifier: Modifier = Modifier,
    contentPadding: PaddingValues = PaddingValues(OpalineSpacing.panelPadding),
    verticalArrangement: Arrangement.Vertical = Arrangement.spacedBy(12.dp),
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier
            .fillMaxWidth()
            .then(rememberOpalineSurface(OpalineBody.Panel))
            .padding(contentPadding)
            .padding(bottom = OpalineBody.Panel.wall),
        verticalArrangement = verticalArrangement,
        content = content,
    )
}

/** A pressable slab that opens something; the whole card is one action. */
@Composable
fun OpalineCard(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    selected: Boolean = false,
    onClickLabel: String? = null,
    contentPadding: PaddingValues = PaddingValues(18.dp),
    content: @Composable ColumnScope.() -> Unit,
) {
    val interaction = remember { MutableInteractionSource() }
    val contact = rememberOpalineContact(interaction, enabled)
    Column(
        modifier
            .then(rememberOpalineSurface(OpalineBody.Slab, contact, selected = selected, enabled = enabled))
            .semantics { if (selected) this.selected = true }
            .clickable(interaction, null, enabled = enabled, onClickLabel = onClickLabel, role = Role.Button, onClick = onClick)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .padding(contentPadding)
            .padding(bottom = OpalineBody.Slab.wall),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        content = content,
    )
}

/**
 * C02 wide slab for one list item. Title and subtitle stay on the stable front plane; trailing
 * actions keep their own semantics and targets.
 */
@Composable
fun OpalineRow(
    title: String,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    onClick: (() -> Unit)? = null,
    onClickLabel: String? = null,
    selected: Boolean = false,
    enabled: Boolean = true,
    titleMaxLines: Int = 1,
    leading: (@Composable () -> Unit)? = null,
    trailing: (@Composable RowScope.() -> Unit)? = null,
) {
    val palette = Opaline.palette
    val interaction = remember { MutableInteractionSource() }
    val contact = rememberOpalineContact(interaction, enabled && onClick != null)
    Row(
        modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = 68.dp)
            .then(rememberOpalineSurface(OpalineBody.Slab, contact, selected = selected, enabled = enabled))
            .semantics { if (selected) this.selected = true }
            .then(
                if (onClick != null) {
                    Modifier.clickable(interaction, null, enabled = enabled, onClickLabel = onClickLabel, role = Role.Button, onClick = onClick)
                } else {
                    Modifier
                },
            ).alpha(if (enabled) 1f else DISABLED_ALPHA)
            .padding(start = 16.dp, end = if (trailing != null) 8.dp else 16.dp, top = 10.dp, bottom = 10.dp + OpalineBody.Slab.wall),
        horizontalArrangement = Arrangement.spacedBy(14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        leading?.invoke()
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(
                title,
                style = MaterialTheme.typography.titleSmall,
                color = palette.text,
                maxLines = titleMaxLines,
                overflow = TextOverflow.Ellipsis,
            )
            if (!subtitle.isNullOrBlank()) {
                Text(
                    subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = palette.textMuted,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        if (trailing != null) {
            Row(verticalAlignment = Alignment.CenterVertically, content = trailing)
        }
    }
}

/** An overline heading for a group of controls, marked as a heading for assistive technology. */
@Composable
fun OpalineSectionHeader(
    title: String,
    modifier: Modifier = Modifier,
    trailing: (@Composable RowScope.() -> Unit)? = null,
) {
    val palette = Opaline.palette
    Row(
        modifier.fillMaxWidth().padding(top = 6.dp, bottom = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Box(
            Modifier.size(8.dp).drawBehind {
                drawCircle(palette.accent, radius = size.minDimension / 2f)
                drawCircle(palette.text.copy(alpha = 0.6f), radius = size.minDimension / 5f, center = center - Offset(1f, 1f) * density)
            },
        )
        Text(
            title.uppercase(),
            Modifier.weight(1f).semantics { heading() },
            style = MaterialTheme.typography.labelMedium,
            color = palette.gel,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        trailing?.invoke(this)
    }
}

/** A calm, centred message for an empty or not-yet-available place, with an optional action. */
@Composable
fun OpalineEmptyState(
    title: String,
    message: String,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    action: (@Composable () -> Unit)? = null,
) {
    val palette = Opaline.palette
    Column(
        modifier.fillMaxWidth().padding(horizontal = 28.dp, vertical = 32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Box(
            Modifier.size(72.dp).then(rememberOpalineSurface(OpalineBody.Bead)).padding(bottom = OpalineBody.Bead.wall),
            contentAlignment = Alignment.Center,
        ) {
            if (icon != null) Icon(icon, null, Modifier.size(30.dp), tint = palette.onGel)
        }
        Spacer(Modifier.height(4.dp))
        Text(title, style = MaterialTheme.typography.titleLarge, color = palette.text, textAlign = TextAlign.Center)
        Text(message, style = MaterialTheme.typography.bodyMedium, color = palette.textMuted, textAlign = TextAlign.Center)
        if (action != null) {
            Spacer(Modifier.height(4.dp))
            action()
        }
    }
}

/** A dismissible status message on a slab; errors are marked with an icon, not only colour. */
@Composable
fun OpalineNotice(
    text: String,
    modifier: Modifier = Modifier,
    error: Boolean = false,
    onDismiss: (() -> Unit)? = null,
) {
    val palette = Opaline.palette
    Row(
        modifier
            .fillMaxWidth()
            .then(rememberOpalineSurface(OpalineBody.Slab, selected = !error))
            .padding(start = 16.dp, end = 6.dp, top = 8.dp, bottom = 8.dp + OpalineBody.Slab.wall),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Icon(
            if (error) Icons.Filled.ErrorOutline else Icons.Filled.Info,
            null,
            Modifier.size(22.dp),
            tint = if (error) palette.error else palette.accent,
        )
        Text(text, Modifier.weight(1f).padding(vertical = 8.dp), style = MaterialTheme.typography.bodyMedium, color = palette.text)
        if (onDismiss != null) {
            OpalineIconButton(Icons.Filled.Close, stringResource(R.string.action_dismiss), onDismiss, emphasis = OpalineEmphasis.Quiet)
        } else {
            Spacer(Modifier.width(10.dp))
        }
    }
}
