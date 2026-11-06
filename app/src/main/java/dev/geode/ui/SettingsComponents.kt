package dev.geode.ui

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.floatingGlass
import dev.geode.ui.theme.selectedGlow

// The building blocks of Settings, in the pattern of the new Customize (CustomizePanel): a list
// of groups, a group's page with ← its title and ‹ › to the groups beside it, a Basics card that is
// always open, then sections that drop open below it, several at once. The cards are Customize's
// own look (floating glass on the canvas, the accent glow when open, the ▾ that turns over), so
// Settings and Customize read as one app. The rows are the standard Android settings row: icon,
// title, one-line summary (prior art: #265's SettingsComponents, reused for its rows only).

/** Settings never grows wider than this; on a tablet the list sits centred (#265, M3 readable width). */
internal val SettingsMaxWidth = 600.dp

/** Alpha for a row the user cannot use right now (disabled, as M3 draws disabled content). */
private const val DISABLED_ALPHA = 0.38f

private const val HALF_TURN = 180f

private val SETTINGS_CARD_ELEVATION = 4.dp

private val SETTINGS_CARD_HEADER_HEIGHT = 56.dp

/**
 * One settings row: an optional leading [icon], the [title] and a one-line [summary], and an
 * optional [trailing] control. Merged for TalkBack, so the title and the summary read as one item.
 */
@Composable
internal fun SettingsRow(
    title: String,
    modifier: Modifier = Modifier,
    summary: String? = null,
    icon: ImageVector? = null,
    enabled: Boolean = true,
    contentPadding: PaddingValues = PaddingValues(vertical = GeodeSpacing.S),
    onClick: (() -> Unit)? = null,
    interaction: Modifier = Modifier,
    trailing: (@Composable () -> Unit)? = null,
) {
    val cs = MaterialTheme.colorScheme
    Row(
        modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = 56.dp)
            .clip(MaterialTheme.shapes.small)
            .then(
                if (onClick != null) Modifier.clickable(enabled = enabled, role = Role.Button, onClick = onClick) else Modifier,
            ).then(interaction)
            .semantics(mergeDescendants = true) {}
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .padding(contentPadding),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.L),
    ) {
        if (icon != null) Icon(icon, contentDescription = null, tint = accentTextColor(), modifier = Modifier.size(24.dp))
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XXS)) {
            Text(title, style = MaterialTheme.typography.bodyLarge, color = cs.onSurface)
            if (!summary.isNullOrBlank()) {
                Text(summary, style = MaterialTheme.typography.bodySmall, color = cs.onSurfaceVariant)
            }
        }
        trailing?.invoke()
    }
}

/** A [SettingsRow] whose whole row toggles a switch, read by TalkBack as one switch. */
@Composable
internal fun SettingsSwitchRow(
    title: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    summary: String? = null,
    enabled: Boolean = true,
) {
    SettingsRow(
        title = title,
        summary = summary,
        enabled = enabled,
        modifier = modifier,
        interaction =
            Modifier.toggleable(
                value = checked,
                enabled = enabled,
                role = Role.Switch,
                onValueChange = onCheckedChange,
            ),
        trailing = { Switch(checked = checked, onCheckedChange = null, enabled = enabled) },
    )
}

/**
 * A pick from a list, as one row (UI V2: dropdowns, not dialogs or chip walls): the title, the
 * current choice as its summary and a ▾; a tap drops the options down from the row, the current
 * one ticked. Choosing closes it.
 */
@Composable
internal fun <T> SettingsChoiceRow(
    title: String,
    options: List<T>,
    selected: T,
    label: @Composable (T) -> String,
    onSelect: (T) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    var open by rememberSaveable { mutableStateOf(false) }
    Box(modifier) {
        SettingsRow(
            title = title,
            summary = label(selected),
            enabled = enabled,
            onClick = { open = true },
            trailing = { Icon(Icons.Filled.ArrowDropDown, contentDescription = null) },
        )
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEach { option ->
                DropdownMenuItem(
                    text = { Text(label(option), style = MaterialTheme.typography.bodyMedium) },
                    onClick = {
                        open = false
                        onSelect(option)
                    },
                    leadingIcon = {
                        // Every item keeps the tick's space, so the labels line up.
                        Icon(
                            StoneIcon.CHECK.vector,
                            contentDescription = null,
                            modifier = Modifier.alpha(if (option == selected) 1f else 0f),
                        )
                    },
                    modifier = Modifier.semantics { this.selected = option == selected },
                )
            }
        }
    }
}

/** A short explanation under a control, in the variant colour. */
@Composable
internal fun SettingsHint(
    text: String,
    modifier: Modifier = Modifier,
) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = modifier)
}

/**
 * A group's header inside its page: ← back to the list, the title, "2 of 9", and ‹ › to the group
 * before and after, as Customize › Advanced steps its groups. Stepping sideways adds no back entry:
 * back always returns to the list.
 */
@Composable
internal fun SettingsGroupHeader(
    title: String,
    index: Int,
    count: Int,
    onBack: () -> Unit,
    onStep: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = modifier.fillMaxWidth().heightIn(min = 48.dp)) {
        IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(R.string.settings_back_to_list))
        }
        Text(title, style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f).semantics { heading() })
        Text(
            stringResource(R.string.settings_group_position, index + 1, count),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        IconButton(onClick = { onStep(index - 1) }, enabled = index > 0) {
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, contentDescription = stringResource(R.string.settings_previous_group))
        }
        IconButton(onClick = { onStep(index + 1) }, enabled = index < count - 1) {
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = stringResource(R.string.settings_next_group))
        }
    }
}

/** Basics: the card at the top of every group, always open, never collapsible. */
@Composable
internal fun SettingsBasicsCard(content: @Composable ColumnScope.() -> Unit) {
    val shape = RoundedCornerShape(GeodeRadius.Large)
    Column(
        Modifier
            .fillMaxWidth()
            .floatingGlass(shape = shape, elevation = SETTINGS_CARD_ELEVATION)
            .padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.M),
        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        CrystalOverline(stringResource(R.string.settings_basics), Modifier.semantics { heading() })
        content()
    }
}

/**
 * One of a group's sections below Basics: its title and a ▾ that turns over; a tap drops its
 * controls open in place, growing from the header with the theme's spatial spring (none under
 * reduced motion). Unlike Customize's cards, opening one leaves the others as they are: Settings
 * is read side by side more than it is edited one card at a time.
 */
@Composable
internal fun SettingsSectionCard(
    title: String,
    expanded: Boolean,
    onToggle: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    val motion = LocalGeodeMotion.current
    val turn by animateFloatAsState(if (expanded) HALF_TURN else 0f, motion.fastSpatialSpec(), label = "section chevron")
    val shape = RoundedCornerShape(GeodeRadius.Large)
    val stateText = stringResource(if (expanded) R.string.customize_card_expanded else R.string.customize_card_collapsed)
    val actionText = stringResource(if (expanded) R.string.customize_card_collapse else R.string.customize_card_expand)
    Column(
        modifier
            .fillMaxWidth()
            .selectedGlow(expanded, shape)
            .floatingGlass(shape = shape, elevation = SETTINGS_CARD_ELEVATION)
            .animateContentSize(motion.defaultSpatialSpec()),
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = SETTINGS_CARD_HEADER_HEIGHT)
                .clickable(role = Role.Button, onClickLabel = actionText, onClick = onToggle)
                .semantics(mergeDescendants = true) {
                    heading()
                    stateDescription = stateText
                }.padding(start = GeodeSpacing.L, end = GeodeSpacing.M),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(title, style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
            Icon(
                Icons.Filled.ExpandMore,
                contentDescription = null,
                modifier = Modifier.rotate(turn),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (expanded) {
            Column(
                Modifier.fillMaxWidth().padding(start = GeodeSpacing.L, end = GeodeSpacing.L, bottom = GeodeSpacing.M),
                verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
                content = content,
            )
        }
    }
}
