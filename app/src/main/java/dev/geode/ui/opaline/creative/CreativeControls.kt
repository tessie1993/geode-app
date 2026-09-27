package dev.geode.ui.opaline.creative

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SheetState
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import dev.geode.ui.opaline.OpalineButton
import dev.geode.ui.opaline.OpalineColors
import dev.geode.ui.opaline.OpalinePanel
import dev.geode.ui.opaline.opalinePart
import kotlin.math.roundToInt

/** Native content planes and semantics attached to the Opaline geometry world. */
object CreativeColors {
    val textPrimary = OpalineColors.text
    val textSecondary = OpalineColors.muted
    val mint = OpalineColors.accent
    val lavender = OpalineColors.lavender
    val peach = OpalineColors.amber
    val pink = OpalineColors.error
    val sky = OpalineColors.gel
    val glassFill = OpalineColors.surface
    val glassRim = OpalineColors.rim
}

object CreativeShapes {
    val tile = RoundedCornerShape(28.dp)
    val pill = RoundedCornerShape(50)
    val bubble = CircleShape
}

object CreativeIcons {
    val Close = Icons.Filled.Close
}

@Suppress("UNUSED_PARAMETER")
fun Modifier.creativeSurface(
    shape: Shape = CreativeShapes.tile,
    tint: Color? = null,
    selected: Boolean = false,
    glow: Float = 0f,
): Modifier = opalinePart("C01", selected = selected).background((tint ?: Color.White).copy(alpha = 0.13f), shape)

@Suppress("UNUSED_PARAMETER")
fun Modifier.creativeFloat(strength: Float = 1f): Modifier = this

@Composable
fun CreativeButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    compact: Boolean = false,
    filled: Boolean = true,
    selected: Boolean = false,
    tint: Color? = null,
    content: @Composable () -> Unit,
) {
    Box(
        modifier
            .defaultMinSize(minWidth = 48.dp, minHeight = 48.dp)
            .opalinePart("A05", selected = selected || filled && tint != null, enabled = enabled)
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = if (compact) 14.dp else 20.dp, vertical = 12.dp),
        contentAlignment = Alignment.Center,
    ) { content() }
}

@Composable
fun CreativeButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    enabled: Boolean = true,
    selected: Boolean = false,
    tint: Color? = null,
) = OpalineButton(text, onClick, modifier, icon = icon, enabled = enabled, selected = selected || tint != null)

@Composable
fun CreativeSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    steps: Int = 0,
    enabled: Boolean = true,
) {
    val span = valueRange.endInclusive - valueRange.start
    val fraction = if (span > 0) ((value - valueRange.start) / span).coerceIn(0f, 1f) else 0f
    Slider(
        value = value.coerceIn(valueRange),
        onValueChange = onValueChange,
        modifier = modifier.opalinePart("B03", value = fraction, enabled = enabled),
        valueRange = valueRange,
        steps = steps,
        enabled = enabled,
        colors =
            SliderDefaults.colors(
                activeTrackColor = Color.Transparent,
                inactiveTrackColor = Color.Transparent,
                thumbColor = Color.Transparent,
            ),
    )
}

@Composable
fun CreativeToggle(
    checked: Boolean,
    onCheckedChange: ((Boolean) -> Unit)?,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    Switch(
        checked = checked,
        onCheckedChange = onCheckedChange,
        enabled = enabled,
        modifier = modifier.opalinePart("B09", value = if (checked) 1f else 0f, selected = checked, enabled = enabled),
        colors =
            SwitchDefaults.colors(
                checkedThumbColor = Color.Transparent,
                uncheckedThumbColor = Color.Transparent,
                checkedTrackColor = Color.Transparent,
                uncheckedTrackColor = Color.Transparent,
                checkedBorderColor = Color.Transparent,
                uncheckedBorderColor = Color.Transparent,
            ),
    )
}

@Composable
fun CreativeKnob(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    enabled: Boolean = true,
    knobSize: Dp = 72.dp,
) {
    val span = valueRange.endInclusive - valueRange.start
    val fraction = if (span > 0) ((value - valueRange.start) / span).coerceIn(0f, 1f) else 0f
    val latest = rememberUpdatedState(value)
    val change = rememberUpdatedState(onValueChange)
    Box(
        modifier
            .size(knobSize)
            .opalinePart("B13", value = fraction, enabled = enabled)
            .semantics {
                progressBarRangeInfo = ProgressBarRangeInfo(value, valueRange)
                if (!enabled) disabled()
                setProgress {
                    if (enabled) {
                        change.value(it.coerceIn(valueRange))
                        true
                    } else {
                        false
                    }
                }
            }.pointerInput(enabled, valueRange) {
                if (enabled) {
                    var accumulated = latest.value
                    detectDragGestures(
                        onDragStart = { accumulated = latest.value },
                        onDrag = { contact, delta ->
                            contact.consume()
                            accumulated = (accumulated - delta.y * span / 220f).coerceIn(valueRange)
                            change.value(accumulated)
                        },
                    )
                }
            },
        contentAlignment = Alignment.Center,
    ) { Text("${(fraction * 100).roundToInt()}", style = MaterialTheme.typography.labelMedium) }
}

@Composable
fun CreativeTabs(
    titles: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
    scrollable: Boolean = true,
) {
    val scrollState = rememberScrollState()
    val rowModifier = if (scrollable) modifier.horizontalScroll(scrollState) else modifier
    Row(rowModifier.padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        titles.forEachIndexed { index, title -> CreativeButton(title, { onSelect(index) }, selected = index == selected) }
    }
}

@Composable
fun CreativeSegments(
    options: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
) = CreativeTabs(options, selected, onSelect, modifier)

@Composable
fun CreativeProgress(
    progress: Float,
    modifier: Modifier = Modifier,
) {
    LinearProgressIndicator(progress = { progress.coerceIn(0f, 1f) }, modifier = modifier.opalinePart("B03", value = progress))
}

@Composable
fun CreativeTextField(
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    placeholder: String? = null,
    enabled: Boolean = true,
    singleLine: Boolean = true,
    keyboardType: KeyboardType = KeyboardType.Text,
    visualTransformation: VisualTransformation = VisualTransformation.None,
) {
    OutlinedTextField(
        value,
        onValueChange,
        modifier.opalinePart("C04"),
        enabled = enabled,
        singleLine = singleLine,
        placeholder = placeholder?.let { { Text(it) } },
        keyboardOptions = KeyboardOptions(keyboardType = keyboardType),
        visualTransformation = visualTransformation,
        shape = RoundedCornerShape(24.dp),
    )
}

@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
@Composable
fun CreativeSheet(
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    sheetState: SheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
    content: @Composable () -> Unit,
) {
    ModalBottomSheet(
        onDismissRequest,
        modifier,
        sheetState,
        containerColor = OpalineColors.surface,
        contentColor = CreativeColors.textPrimary,
    ) {
        Column(Modifier.fillMaxWidth().padding(bottom = 24.dp)) { content() }
    }
}

@Composable
fun CreativeDialog(
    onDismissRequest: () -> Unit,
    title: String,
    modifier: Modifier = Modifier,
    text: String? = null,
    actions: @Composable () -> Unit = {},
) {
    Dialog(onDismissRequest) {
        OpalinePanel(modifier.widthIn(min = 280.dp, max = 440.dp)) {
            Text(title, style = MaterialTheme.typography.headlineSmall)
            text?.let { Text(it, style = MaterialTheme.typography.bodyMedium) }
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) { actions() }
        }
    }
}

@Composable
fun CreativeTile(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    contentPadding: PaddingValues = PaddingValues(16.dp),
    content: @Composable ColumnScope.() -> Unit,
) {
    OpalinePanel(modifier.clickable(onClick = onClick)) { Column(Modifier.padding(contentPadding), content = content) }
}

@Composable
fun CreativeTopBar(title: String) {
    Text(title, Modifier.padding(20.dp), style = MaterialTheme.typography.headlineMedium)
}
