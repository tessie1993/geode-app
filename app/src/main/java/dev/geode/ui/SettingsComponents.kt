package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ArrowDropDown
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.pro.FeatureGate
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon

// The building blocks of every Settings page: the standard Android settings row (icon, title,
// one-line summary) and its switch and single-choice forms. The Pro mark is
// Customize's own ProCrown (CustomizeAdvanced), so the app draws one crown.
// Plain Material 3 on the black-glass theme: no glows, the theme's own type and colours.

/** Settings never grows wider than this; on a tablet the list sits centred. */
internal val SettingsMaxWidth = 600.dp

/** Alpha for a row the user cannot use right now (disabled, as M3 draws disabled content). */
private const val DISABLED_ALPHA = 0.38f

/**
 * One settings row: an optional leading [icon], the [title] and a one-line [summary], and an
 * optional [trailing] control. A [pro] row is drawn bolder with a crown; what tapping it does
 * without Pro is the caller's (see [ProAccess.runOrPaywall]).
 */
@Composable
internal fun SettingsRow(
    title: String,
    modifier: Modifier = Modifier,
    summary: String? = null,
    icon: ImageVector? = null,
    pro: Boolean = false,
    enabled: Boolean = true,
    contentPadding: PaddingValues = PaddingValues(vertical = GeodeSpacing.S),
    onClick: (() -> Unit)? = null,
    interaction: Modifier = Modifier,
    trailing: (@Composable () -> Unit)? = null,
) {
    val cs = MaterialTheme.colorScheme
    // The click and [interaction] sit inside the clip, so the ripple keeps the row's shape. Merged,
    // so TalkBack reads the title, the crown and the summary as one item; a button in [trailing]
    // stays its own.
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
        if (icon != null) Icon(icon, contentDescription = null, tint = cs.onSurfaceVariant, modifier = Modifier.size(24.dp))
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XXS)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.XS)) {
                Text(
                    title,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = if (pro) FontWeight.SemiBold else null,
                    color = cs.onSurface,
                    modifier = Modifier.weight(1f, fill = false),
                )
                if (pro) ProCrown()
            }
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
    pro: Boolean = false,
) {
    SettingsRow(
        title = title,
        summary = summary,
        pro = pro,
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
 * A pick from a list, as one row (the UI V2 rule: dropdowns, not dialogs or chip walls): the
 * title, the current choice as its summary and a ▾; a tap drops the options down from the row,
 * the current one ticked. Choosing closes it. [proOption] marks the choices that need Pro with a
 * crown; [onSelect] decides what happens when a free user picks one.
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
    icon: ImageVector? = null,
    proOption: (T) -> Boolean = { false },
) {
    var open by rememberSaveable { mutableStateOf(false) }
    Box(modifier) {
        SettingsRow(
            title = title,
            summary = label(selected),
            icon = icon,
            enabled = enabled,
            onClick = { open = true },
            trailing = { Icon(Icons.Rounded.ArrowDropDown, contentDescription = null) },
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
                    trailingIcon = { if (proOption(option)) ProCrown() },
                    modifier = Modifier.semantics { this.selected = option == selected },
                )
            }
        }
    }
}

/** The primary-coloured Label Large title over a run of rows inside a page. */
@Composable
internal fun SettingsSectionTitle(
    text: String,
    modifier: Modifier = Modifier,
) {
    Text(
        text,
        style = MaterialTheme.typography.labelLarge,
        color = MaterialTheme.colorScheme.primary,
        modifier = modifier.semantics { heading() },
    )
}

/** A short explanation under a control, in the variant colour. */
@Composable
internal fun SettingsHint(
    text: String,
    modifier: Modifier = Modifier,
) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = modifier)
}

/** Whether the user has a Pro feature, and the one way a Settings row asks for it. */
internal class ProAccess(
    private val gate: FeatureGate,
    private val paywall: PaywallViewModel,
) {
    fun allows(feature: ProFeature): Boolean = gate.allows(feature)

    /** Runs [action] when [feature] is unlocked; otherwise opens the paywall for it. */
    fun runOrPaywall(
        feature: ProFeature,
        action: () -> Unit,
    ) {
        if (gate.allows(feature)) action() else paywall.open(PaywallTrigger.Feature(feature))
    }
}

@Composable
internal fun rememberProAccess(): ProAccess {
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    return ProAccess(gate, paywall)
}
