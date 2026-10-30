package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.PrimaryScrollableTabRow
import androidx.compose.material3.ProvideTextStyle
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Slider
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.ripple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.ui.theme.GeodeColors
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.UiSound
import dev.geode.ui.theme.rememberStoneInteraction
import dev.geode.ui.theme.stonePress

/**
 * The app's button. [filled] is the tonal glass button for a screen's main actions; unfilled is
 * the outlined one for everything beside them; [compact] is the dense variant for rows and chips.
 */
@Composable
fun CrystalButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    filled: Boolean = true,
    compact: Boolean = false,
    content: @Composable RowScope.() -> Unit,
) {
    val cs = MaterialTheme.colorScheme
    val interaction = rememberStoneInteraction()
    val sounds = LocalUiSounds.current
    val shape = MaterialTheme.shapes.extraLarge
    val fill = if (filled) Color.White.copy(alpha = GeodeGlass.FILL_RAISED) else Color.Transparent
    val edge =
        when {
            !enabled -> Color.White.copy(alpha = GeodeGlass.EDGE * 0.5f)
            filled -> cs.primary.copy(alpha = 0.32f)
            else -> Color.White.copy(alpha = GeodeGlass.EDGE * 1.6f)
        }
    Box(
        modifier
            .stonePress(interaction)
            .defaultMinSize(minHeight = if (compact) 36.dp else GeodeSpacing.TouchTarget)
            .clip(shape)
            .background(fill)
            .border(1.dp, edge, shape)
            .clickable(
                interactionSource = interaction,
                indication = ripple(),
                enabled = enabled,
                onClick = {
                    sounds.play(if (filled && !compact) UiSound.CONFIRM else UiSound.TAP)
                    onClick()
                },
            ),
    ) {
        Row(
            Modifier
                .align(Alignment.Center)
                .padding(horizontal = if (compact) 14.dp else 22.dp, vertical = if (compact) 6.dp else 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S, Alignment.CenterHorizontally),
        ) {
            val label =
                (LocalFontColor.current ?: if (filled) cs.onSurface else cs.primary)
                    .copy(alpha = if (enabled) 1f else GeodeGlass.DISABLED_CONTENT)
            CompositionLocalProvider(LocalContentColor provides label) {
                ProvideTextStyle(MaterialTheme.typography.labelLarge) {
                    content()
                }
            }
        }
    }
}

/** The one solid control in the app: the round accent play/pause button. */
@Composable
fun CrystalPlayButton(
    icon: StoneIcon,
    contentDescription: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val cs = MaterialTheme.colorScheme
    val interaction = rememberStoneInteraction()
    val sounds = LocalUiSounds.current
    Box(
        modifier
            .size(64.dp)
            .stonePress(interaction)
            .clip(CircleShape)
            .background(if (enabled) cs.primary else cs.onSurface.copy(alpha = 0.12f))
            .clickable(
                interactionSource = interaction,
                indication = ripple(color = cs.onPrimary),
                enabled = enabled,
                onClick = {
                    sounds.play(UiSound.TAP)
                    onClick()
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        StoneIconArt(
            icon,
            contentDescription,
            Modifier.size(30.dp),
            tint = if (enabled) cs.onPrimary else cs.onSurface.copy(alpha = GeodeGlass.DISABLED_CONTENT),
        )
    }
}

@Composable
fun CrystalSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    steps: Int = 0,
    enabled: Boolean = true,
) {
    Slider(
        value = value,
        onValueChange = onValueChange,
        modifier = modifier,
        valueRange = valueRange,
        steps = steps,
        enabled = enabled,
    )
}

data class CrystalNavItem(
    val label: String,
    val icon: StoneIcon,
)

/** Bottom navigation: Material's NavigationBar on a glass bar whose opacity the Look setting picks. */
@Composable
fun CrystalNavBar(
    items: List<CrystalNavItem>,
    selected: Int,
    onSelect: (Int) -> Unit,
    opacity: Float,
) {
    val cs = MaterialTheme.colorScheme
    val sounds = LocalUiSounds.current
    NavigationBar(
        containerColor = GeodeColors.Surface.copy(alpha = (0.55f + 0.45f * opacity).coerceIn(0f, 1f)),
        tonalElevation = 0.dp,
    ) {
        items.forEachIndexed { i, item ->
            NavigationBarItem(
                selected = i == selected,
                onClick = {
                    if (i != selected) sounds.play(UiSound.SWOOP)
                    onSelect(i)
                },
                icon = { StoneIconArt(item.icon, null) },
                label = { Text(item.label, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                colors =
                    NavigationBarItemDefaults.colors(
                        selectedIconColor = cs.onPrimaryContainer,
                        selectedTextColor = accentTextColor(),
                        indicatorColor = cs.primaryContainer,
                        unselectedIconColor = cs.onSurfaceVariant,
                        unselectedTextColor = cs.onSurfaceVariant,
                    ),
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CrystalTabs(
    titles: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val cs = MaterialTheme.colorScheme
    PrimaryScrollableTabRow(
        selectedTabIndex = selected,
        modifier = modifier,
        edgePadding = GeodeSpacing.S,
        containerColor = Color.Transparent,
        contentColor = accentTextColor(),
        divider = { Box(Modifier.fillMaxWidth().height(1.dp).background(cs.outlineVariant)) },
    ) {
        titles.forEachIndexed { i, title ->
            val sel = i == selected
            Tab(
                selected = sel,
                onClick = { onSelect(i) },
                selectedContentColor = accentTextColor(),
                unselectedContentColor = cs.onSurfaceVariant,
                text = {
                    Text(
                        title,
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = if (sel) FontWeight.SemiBold else FontWeight.Normal,
                    )
                },
            )
        }
    }
}

@Composable
fun CrystalSegmented(
    options: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val cs = MaterialTheme.colorScheme
    val sounds = LocalUiSounds.current
    SingleChoiceSegmentedButtonRow(modifier.fillMaxWidth()) {
        options.forEachIndexed { i, label ->
            SegmentedButton(
                selected = i == selected,
                onClick = {
                    sounds.play(UiSound.TAP)
                    onSelect(i)
                },
                shape = SegmentedButtonDefaults.itemShape(i, options.size),
                modifier = Modifier.weight(1f),
                icon = {},
                colors =
                    SegmentedButtonDefaults.colors(
                        activeContainerColor = cs.primaryContainer,
                        activeContentColor = cs.onPrimaryContainer,
                        inactiveContainerColor = Color.Transparent,
                        inactiveContentColor = LocalFontColor.current ?: cs.onSurface,
                        activeBorderColor = cs.outline,
                        inactiveBorderColor = cs.outline,
                    ),
            ) {
                Text(
                    label,
                    style = MaterialTheme.typography.labelMedium,
                    textAlign = TextAlign.Center,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}
