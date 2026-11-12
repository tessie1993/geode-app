package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.PrimaryScrollableTabRow
import androidx.compose.material3.ProvideTextStyle
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.minimumInteractiveComponentSize
import androidx.compose.material3.ripple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.UiSound
import dev.geode.ui.theme.rememberStoneInteraction
import dev.geode.ui.theme.stonePress
import kotlin.math.roundToInt

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
            // A compact button draws 36dp tall but still answers to a 48dp touch target.
            .minimumInteractiveComponentSize()
            .stonePress(interaction)
            .defaultMinSize(minHeight = if (compact) 36.dp else GeodeSpacing.TouchTarget)
            .clip(shape)
            .background(fill)
            .border(1.dp, edge, shape)
            .clickable(
                interactionSource = interaction,
                indication = ripple(),
                enabled = enabled,
                role = Role.Button,
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

/**
 * The app's slider. [label] is the row's line, its name with the value and unit ("Speed 1.25x"),
 * drawn above the track. TalkBack reads it as the slider's value ([valueDescription]), so the
 * slider is one stop that says what it is and where it stands, and says the new line as it moves;
 * the drawn line stays silent so the name is not read twice. A bare Material slider announces
 * only a percentage of its track (#996).
 *
 * [accessibilityStep] is one TalkBack swipe or volume key press, in the value's units, for a
 * slider whose caller snaps to a grid but has no ticks (0.05x for speed): the default step is a
 * twentieth of the track, which a coarser snap can round back to where it was.
 */
@Composable
fun CrystalSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    steps: Int = 0,
    enabled: Boolean = true,
    onValueChangeFinished: (() -> Unit)? = null,
    label: String? = null,
    labelStyle: TextStyle = MaterialTheme.typography.labelMedium,
    valueDescription: String? = label,
    accessibilityStep: Float = 0f,
) {
    val span = valueRange.endInclusive - valueRange.start
    val a11ySteps =
        if (steps == 0 && accessibilityStep > 0f && span > 0f) {
            ((span / accessibilityStep).roundToInt() - 1).coerceAtLeast(0)
        } else {
            steps
        }
    val slider: @Composable (Modifier) -> Unit = { sliderModifier ->
        Slider(
            value = value,
            onValueChange = onValueChange,
            // A thumb at 0 sits in the back-gesture strip; without this a drag from it leaves the screen.
            modifier =
                sliderModifier.systemGestureExclusion().semantics {
                    if (valueDescription != null) stateDescription = valueDescription
                    if (a11ySteps != steps) {
                        progressBarRangeInfo = ProgressBarRangeInfo(value.coerceIn(valueRange), valueRange, a11ySteps)
                    }
                },
            valueRange = valueRange,
            steps = steps,
            enabled = enabled,
            onValueChangeFinished = onValueChangeFinished,
        )
    }
    if (label == null) {
        slider(modifier)
    } else {
        Column(modifier) {
            Text(label, style = labelStyle, modifier = Modifier.clearAndSetSemantics {})
            slider(Modifier)
        }
    }
}

/**
 * A switch with its label as one control: the whole row toggles, and TalkBack reads the label with
 * the state ("Interface sounds, on, switch"). A bare Switch next to a Text announces only "on".
 */
@Composable
fun LabeledSwitch(
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    style: TextStyle = MaterialTheme.typography.bodyMedium,
) {
    Row(
        modifier
            .fillMaxWidth()
            .toggleable(value = checked, enabled = enabled, role = Role.Switch, onValueChange = onCheckedChange)
            .minimumInteractiveComponentSize(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Text(label, Modifier.weight(1f), style = style)
        Switch(checked = checked, onCheckedChange = null, enabled = enabled)
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
