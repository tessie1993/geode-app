package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfo
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.window.core.layout.WindowSizeClass
import dev.geode.R
import dev.geode.data.BackgroundPrefs
import dev.geode.data.BackgroundPrefsStore
import dev.geode.render.UnderlayBlend
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import kotlin.math.roundToInt

/**
 * Picks the background image behind the scene, and its blend/amount/blur/dim.
 *
 * Choose and Remove stay at the top, always in view: they are what the sheet is for. Below them
 * the four options are an overview first: Blend is one dropdown row with its value, and Amount,
 * Blur and Dim are collapsed cards that show their value and open their slider in place, one at
 * a time, so the sheet stays short enough to read at a glance. On a wide or landscape window the
 * cards sit in two columns instead of one long column the sheet would have to scroll.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BackgroundSheet(onDismiss: () -> Unit) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val prefs by visualsViewModel.backgroundPrefs.collectAsStateWithLifecycle()
    val picker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) visualsViewModel.pickBackgroundImage(uri)
        }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            BackgroundActions(
                hasImage = prefs.uri != null,
                onPick = { picker.launch(arrayOf("image/*")) },
                onClear = visualsViewModel::clearBackgroundImage,
            )
            BackgroundOptions(prefs, visualsViewModel)
        }
    }
}

/** The title, the one line when there is no image, and Choose and Remove: always in view. */
@Composable
private fun BackgroundActions(
    hasImage: Boolean,
    onPick: () -> Unit,
    onClear: () -> Unit,
) {
    Text(stringResource(R.string.background_sheet_title), style = MaterialTheme.typography.titleMedium)
    if (!hasImage) {
        Text(
            stringResource(R.string.background_none),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        FilledTonalButton(onClick = onPick, contentPadding = CompactButtonPadding) {
            Text(stringResource(R.string.background_pick))
        }
        if (hasImage) {
            FilledTonalButton(onClick = onClear, contentPadding = CompactButtonPadding) {
                Text(stringResource(R.string.background_clear))
            }
        }
    }
}

/** Blend, Amount, Blur and Dim as an overview: one card open at a time, two columns when wide. */
@Composable
private fun BackgroundOptions(
    prefs: BackgroundPrefs,
    visualsViewModel: VisualsViewModel,
) {
    var openCard by rememberSaveable { mutableStateOf<String?>(null) }
    val toggle: (String) -> Unit = { key -> openCard = if (openCard == key) null else key }
    val wide =
        currentWindowAdaptiveInfo().windowSizeClass.isWidthAtLeastBreakpoint(WindowSizeClass.WIDTH_DP_MEDIUM_LOWER_BOUND)

    val blend: @Composable () -> Unit = {
        BlendRow(prefs.blend, visualsViewModel::setBackgroundBlend)
    }
    val amount: @Composable () -> Unit = {
        SliderCard(
            text = stringResource(R.string.background_amount, (prefs.amount * 100).roundToInt()),
            expanded = openCard == CARD_AMOUNT,
            onToggle = { toggle(CARD_AMOUNT) },
        ) {
            // A thumb at 0 sits in the back-gesture strip; without the exclusion a drag from it leaves the sheet.
            Slider(
                value = prefs.amount,
                onValueChange = visualsViewModel::setBackgroundAmount,
                modifier = Modifier.systemGestureExclusion(),
                valueRange = 0f..1f,
            )
        }
    }
    val blur: @Composable () -> Unit = {
        SliderCard(
            text = stringResource(R.string.background_blur, prefs.blurRadius),
            expanded = openCard == CARD_BLUR,
            onToggle = { toggle(CARD_BLUR) },
        ) {
            Slider(
                value = prefs.blurRadius.toFloat(),
                onValueChange = { visualsViewModel.setBackgroundBlurRadius(it.roundToInt()) },
                modifier = Modifier.systemGestureExclusion(),
                valueRange = BackgroundPrefsStore.BLUR_RANGE.first.toFloat()..BackgroundPrefsStore.BLUR_RANGE.last.toFloat(),
            )
        }
    }
    val dim: @Composable () -> Unit = {
        SliderCard(
            text = stringResource(R.string.background_dim, (prefs.dim * 100).roundToInt()),
            expanded = openCard == CARD_DIM,
            onToggle = { toggle(CARD_DIM) },
        ) {
            Slider(
                value = prefs.dim,
                onValueChange = visualsViewModel::setBackgroundDim,
                modifier = Modifier.systemGestureExclusion(),
                valueRange = 0f..1f,
            )
        }
    }

    if (wide) {
        Row(horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.M)) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
                blend()
                amount()
            }
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
                blur()
                dim()
            }
        }
    } else {
        Column(verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
            blend()
            amount()
            blur()
            dim()
        }
    }
}

/** Blend is a pick from three, so it is one row with its value and ▾ rather than three buttons. */
@Composable
private fun BlendRow(
    current: UnderlayBlend,
    onSelect: (UnderlayBlend) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    OptionSurface {
        Box {
            OptionHeader(
                name = stringResource(R.string.background_blend),
                value = stringResource(blendLabel(current)),
                modifier = Modifier.clickable(role = Role.DropdownList) { open = true },
            ) {
                Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
            }
            DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                UnderlayBlend.entries.forEach { option ->
                    DropdownMenuItem(
                        text = { Text(stringResource(blendLabel(option))) },
                        leadingIcon = {
                            if (option == current) Icon(Icons.Filled.Check, contentDescription = null)
                        },
                        onClick = {
                            open = false
                            onSelect(option)
                        },
                    )
                }
            }
        }
    }
}

/**
 * One slider as a collapsed card: the option's name and value in the header, the slider below it
 * once opened. [text] is the existing "Name: value" string, split at its colon so the value can sit
 * on the right; a wording without a colon shows whole as the name.
 */
@Composable
private fun SliderCard(
    text: String,
    expanded: Boolean,
    onToggle: () -> Unit,
    slider: @Composable () -> Unit,
) {
    val motion = LocalGeodeMotion.current
    val turn by animateFloatAsState(if (expanded) 180f else 0f, motion.defaultSpatialSpec(), label = "chevron")
    val name = text.substringBefore(':').trim()
    val value = text.substringAfter(':', "").trim()
    OptionSurface {
        Column {
            OptionHeader(
                name = name,
                value = value,
                modifier =
                    Modifier
                        .clickable(role = Role.Button, onClick = onToggle)
                        .semantics { stateDescription = if (expanded) "Expanded" else "Collapsed" },
            ) {
                Icon(Icons.Filled.ExpandMore, contentDescription = null, modifier = Modifier.rotate(turn))
            }
            AnimatedVisibility(
                visible = expanded,
                enter = expandVertically(motion.defaultSpatialSpec()) + fadeIn(motion.defaultEffectsSpec()),
                exit = shrinkVertically(motion.defaultSpatialSpec()) + fadeOut(motion.defaultEffectsSpec()),
            ) {
                Box(Modifier.padding(start = GeodeSpacing.L, end = GeodeSpacing.L, bottom = GeodeSpacing.S)) {
                    slider()
                }
            }
        }
    }
}

/** A card's one line: name at the start, the current value at the end, then its ▾. */
@Composable
private fun OptionHeader(
    name: String,
    value: String,
    modifier: Modifier = Modifier,
    chevron: @Composable () -> Unit,
) {
    Row(
        modifier
            .fillMaxWidth()
            .heightIn(min = GeodeSpacing.TouchTarget)
            .padding(start = GeodeSpacing.L, end = GeodeSpacing.S),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Text(
            name,
            Modifier.weight(1f),
            style = MaterialTheme.typography.titleSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        if (value.isNotEmpty()) {
            // Shrinkable and ellipsized (#558): at a large font a long value gives way instead of
            // pushing the name and the chevron out of the row.
            Text(
                value,
                Modifier.weight(1f, fill = false),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        chevron()
    }
}

/** Plain matte card behind each option. */
@Composable
private fun OptionSurface(content: @Composable () -> Unit) {
    Surface(
        shape = RoundedCornerShape(GeodeRadius.Medium),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        modifier = Modifier.fillMaxWidth(),
        content = content,
    )
}

private fun blendLabel(blend: UnderlayBlend): Int =
    when (blend) {
        UnderlayBlend.SCREEN -> R.string.background_blend_screen
        UnderlayBlend.MULTIPLY -> R.string.background_blend_multiply
        UnderlayBlend.ADD -> R.string.background_blend_add
    }

/** The old compact button's padding: these sit in tight rows in a sheet. */
private val CompactButtonPadding = PaddingValues(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS)

private const val CARD_AMOUNT = "amount"
private const val CARD_BLUR = "blur"
private const val CARD_DIM = "dim"
