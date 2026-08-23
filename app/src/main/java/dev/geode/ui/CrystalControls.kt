package dev.geode.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ProvideTextStyle
import androidx.compose.material3.ScrollableTabRow
import androidx.compose.material3.Slider
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.geode.ui.theme.StoneComponent
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.StoneState
import dev.geode.ui.theme.StoneSurfaceArt
import dev.geode.ui.theme.rememberStoneInteraction
import dev.geode.ui.theme.rememberStoneState
import dev.geode.ui.theme.stonePress

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
    val state = rememberStoneState(interaction, enabled = enabled)
    val component =
        when {
            compact -> StoneComponent.COMPACT_BUTTON
            filled -> StoneComponent.PRIMARY_BUTTON
            else -> StoneComponent.SECONDARY_BUTTON
        }
    Box(
        modifier
            .stonePress(interaction)
            .clickable(interactionSource = interaction, indication = null, enabled = enabled, onClick = onClick)
            .defaultMinSize(minHeight = if (compact) 36.dp else 48.dp),
    ) {
        StoneSurfaceArt(component, state, Modifier.matchParentSize())
        Row(
            Modifier
                .align(Alignment.Center)
                .padding(horizontal = if (compact) 16.dp else 22.dp, vertical = if (compact) 6.dp else 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center,
        ) {
            val label = (LocalFontColor.current ?: cs.onSurface).copy(alpha = if (enabled) 1f else 0.55f)
            CompositionLocalProvider(LocalContentColor provides label) {
                ProvideTextStyle(MaterialTheme.typography.labelLarge.copy(letterSpacing = 0.8.sp)) {
                    content()
                }
            }
        }
    }
}

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
    val state = rememberStoneState(interaction, enabled = enabled)
    Box(
        modifier
            .size(60.dp)
            .stonePress(interaction)
            .then(if (enabled) Modifier.softGlow(cs.primary, 14.dp, 0.5f) else Modifier)
            .clickable(interactionSource = interaction, indication = null, enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        StoneSurfaceArt(StoneComponent.ICON_BUTTON, state, Modifier.matchParentSize())
        StoneIconArt(
            icon,
            contentDescription,
            tint = (LocalFontColor.current ?: cs.onSurface).copy(alpha = if (enabled) 1f else 0.55f),
        )
    }
}

@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
@Composable
fun CrystalSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    steps: Int = 0,
    enabled: Boolean = true,
) {
    val cs = MaterialTheme.colorScheme
    val interaction = rememberStoneInteraction()
    val thumbState = rememberStoneState(interaction, enabled = enabled)
    Slider(
        value = value,
        onValueChange = onValueChange,
        modifier = modifier,
        valueRange = valueRange,
        steps = steps,
        enabled = enabled,
        interactionSource = interaction,
        thumb = {
            StoneSurfaceArt(StoneComponent.SLIDER_THUMB, thumbState, Modifier.size(24.dp))
        },
        track = {
            val span = valueRange.endInclusive - valueRange.start
            val fraction = if (span > 0f) ((value - valueRange.start) / span).coerceIn(0f, 1f) else 0f
            val dim = if (enabled) 1f else 0.4f
            Box(Modifier.fillMaxWidth().height(14.dp)) {
                StoneSurfaceArt(
                    StoneComponent.SLIDER_TRACK,
                    if (enabled) StoneState.DEFAULT else StoneState.DISABLED,
                    Modifier.matchParentSize(),
                )
                Canvas(Modifier.matchParentSize()) {
                    val y = size.height / 2f
                    if (steps > 0) {
                        for (i in 1..steps) {
                            val x = size.width * i / (steps + 1)
                            drawCircle(
                                cs.onSurface.copy(alpha = 0.30f * dim),
                                radius = 1.5.dp.toPx(),
                                center = Offset(x, y),
                            )
                        }
                    }
                    if (fraction > 0f) {
                        val endX = size.width * fraction
                        drawLine(
                            brush =
                                Brush.horizontalGradient(
                                    listOf(cs.secondary, cs.primary),
                                    endX = endX.coerceAtLeast(1f),
                                ),
                            start = Offset(0f, y),
                            end = Offset(endX, y),
                            strokeWidth = 3.5.dp.toPx(),
                            cap = StrokeCap.Round,
                        )
                        drawLine(
                            color = cs.primary.copy(alpha = 0.20f * dim),
                            start = Offset(0f, y),
                            end = Offset(endX, y),
                            strokeWidth = 8.dp.toPx(),
                            cap = StrokeCap.Round,
                        )
                    }
                }
            }
        },
    )
}

data class CrystalNavItem(
    val label: String,
    val icon: StoneIcon,
)

@Composable
fun CrystalNavBar(
    items: List<CrystalNavItem>,
    selected: Int,
    onSelect: (Int) -> Unit,
    opacity: Float,
) {
    val cs = MaterialTheme.colorScheme
    Box(Modifier.fillMaxWidth().graphicsLayer { alpha = (0.55f + 0.45f * opacity).coerceIn(0f, 1f) }) {
        StoneSurfaceArt(StoneComponent.NAVIGATION_BAR, StoneState.DEFAULT, Modifier.matchParentSize())
        Row(
            Modifier
                .fillMaxWidth()
                .windowInsetsPadding(WindowInsets.navigationBars)
                .height(68.dp)
                .selectableGroup(),
        ) {
            val selectedLabel = accentTextColor()
            items.forEachIndexed { i, item ->
                val sel = i == selected
                val tint by animateColorAsState(
                    if (sel) cs.primary else cs.onSurfaceVariant.copy(alpha = 0.75f),
                    label = "crystalNavTint",
                )
                val labelTint by animateColorAsState(
                    if (sel) selectedLabel else cs.onSurfaceVariant.copy(alpha = 0.75f),
                    label = "crystalNavLabel",
                )
                val gemAlpha by animateFloatAsState(if (sel) 1f else 0f, label = "crystalNavGem")
                Column(
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .selectable(selected = sel, role = Role.Tab, onClick = { onSelect(i) }),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    Box(Modifier.size(7.dp).graphicsLayer { alpha = gemAlpha }) {
                        CrystalGem(cs.primary, size = 7.dp)
                    }
                    Spacer(Modifier.height(3.dp))
                    StoneIconArt(item.icon, item.label, tint = tint)
                    Spacer(Modifier.height(2.dp))
                    Text(
                        item.label,
                        style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 1.2.sp),
                        fontWeight = if (sel) FontWeight.SemiBold else FontWeight.Normal,
                        color = labelTint,
                    )
                }
            }
        }
    }
}

@Composable
fun CrystalTabs(
    titles: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val cs = MaterialTheme.colorScheme
    ScrollableTabRow(
        selectedTabIndex = selected,
        modifier = modifier,
        edgePadding = 8.dp,
        containerColor = Color.Transparent,
        indicator = { },
        divider = {
            Box(Modifier.fillMaxWidth().height(1.dp).luminousHairline(cs.primary.copy(alpha = 0.45f)))
        },
    ) {
        titles.forEachIndexed { i, title ->
            val sel = i == selected
            Tab(
                selected = sel,
                onClick = { onSelect(i) },
                selectedContentColor = accentTextColor(),
                unselectedContentColor = cs.onSurfaceVariant.copy(alpha = 0.7f),
                text = {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(
                            title,
                            style = MaterialTheme.typography.titleSmall,
                            fontWeight = if (sel) FontWeight.SemiBold else FontWeight.Normal,
                        )
                        Spacer(Modifier.height(3.dp))
                        val gemAlpha by animateFloatAsState(if (sel) 1f else 0f, label = "crystalTabGem")
                        Box(Modifier.size(5.dp).graphicsLayer { alpha = gemAlpha }) {
                            CrystalGem(cs.primary, size = 5.dp)
                        }
                    }
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
    Row(modifier, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        options.forEachIndexed { i, label ->
            val sel = i == selected
            val interaction = rememberStoneInteraction()
            val state = rememberStoneState(interaction, selected = sel)
            Box(
                Modifier
                    .weight(1f)
                    .stonePress(interaction)
                    .defaultMinSize(minHeight = 40.dp)
                    .selectable(
                        selected = sel,
                        role = Role.RadioButton,
                        interactionSource = interaction,
                        indication = null,
                        onClick = { onSelect(i) },
                    ),
                contentAlignment = Alignment.Center,
            ) {
                StoneSurfaceArt(StoneComponent.CHIP, state, Modifier.matchParentSize())
                Text(
                    label,
                    Modifier.padding(horizontal = 10.dp, vertical = 8.dp),
                    style = MaterialTheme.typography.labelMedium,
                    textAlign = TextAlign.Center,
                    fontWeight = if (sel) FontWeight.SemiBold else FontWeight.Normal,
                    color =
                        (LocalFontColor.current ?: cs.onSurface)
                            .copy(alpha = if (sel) 1f else 0.75f),
                )
            }
        }
    }
}
