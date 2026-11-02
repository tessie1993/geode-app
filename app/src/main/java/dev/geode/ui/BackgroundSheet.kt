package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.BackgroundPrefsStore
import dev.geode.render.UnderlayBlend
import dev.geode.ui.theme.GeodeSpacing
import kotlin.math.roundToInt

/** Picks the background image behind the scene, and its blend/amount/blur/dim. */
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
            Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(stringResource(R.string.background_sheet_title), style = MaterialTheme.typography.titleMedium)
            if (prefs.uri == null) {
                Text(
                    stringResource(R.string.background_none),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilledTonalButton(onClick = { picker.launch(arrayOf("image/*")) }, contentPadding = CompactButtonPadding) {
                    Text(stringResource(R.string.background_pick))
                }
                if (prefs.uri != null) {
                    FilledTonalButton(onClick = visualsViewModel::clearBackgroundImage, contentPadding = CompactButtonPadding) {
                        Text(stringResource(R.string.background_clear))
                    }
                }
            }
            Column {
                Text(stringResource(R.string.background_blend), style = MaterialTheme.typography.labelMedium)
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    blendOption(UnderlayBlend.SCREEN, R.string.background_blend_screen, prefs.blend, visualsViewModel::setBackgroundBlend)
                    blendOption(
                        UnderlayBlend.MULTIPLY,
                        R.string.background_blend_multiply,
                        prefs.blend,
                        visualsViewModel::setBackgroundBlend,
                    )
                    blendOption(UnderlayBlend.ADD, R.string.background_blend_add, prefs.blend, visualsViewModel::setBackgroundBlend)
                }
            }
            Column {
                Text(
                    stringResource(R.string.background_amount, (prefs.amount * 100).roundToInt()),
                    style = MaterialTheme.typography.labelMedium,
                )
                // A thumb at 0 sits in the back-gesture strip; without the exclusion a drag from it leaves the sheet.
                Slider(
                    value = prefs.amount,
                    onValueChange = visualsViewModel::setBackgroundAmount,
                    modifier = Modifier.systemGestureExclusion(),
                    valueRange = 0f..1f,
                )
            }
            Column {
                Text(stringResource(R.string.background_blur, prefs.blurRadius), style = MaterialTheme.typography.labelMedium)
                Slider(
                    value = prefs.blurRadius.toFloat(),
                    onValueChange = { visualsViewModel.setBackgroundBlurRadius(it.roundToInt()) },
                    modifier = Modifier.systemGestureExclusion(),
                    valueRange = BackgroundPrefsStore.BLUR_RANGE.first.toFloat()..BackgroundPrefsStore.BLUR_RANGE.last.toFloat(),
                )
            }
            Column {
                Text(
                    stringResource(R.string.background_dim, (prefs.dim * 100).roundToInt()),
                    style = MaterialTheme.typography.labelMedium,
                )
                Slider(
                    value = prefs.dim,
                    onValueChange = visualsViewModel::setBackgroundDim,
                    modifier = Modifier.systemGestureExclusion(),
                    valueRange = 0f..1f,
                )
            }
        }
    }
}

@Composable
private fun blendOption(
    option: UnderlayBlend,
    labelRes: Int,
    current: UnderlayBlend,
    onSelect: (UnderlayBlend) -> Unit,
) {
    val label: @Composable RowScope.() -> Unit = {
        Text(stringResource(labelRes), style = MaterialTheme.typography.bodySmall)
    }
    if (current == option) {
        Button(onClick = { onSelect(option) }, contentPadding = CompactButtonPadding, content = label)
    } else {
        FilledTonalButton(onClick = { onSelect(option) }, contentPadding = CompactButtonPadding, content = label)
    }
}

/** The old compact button's padding: these sit in tight rows in a sheet. */
private val CompactButtonPadding = PaddingValues(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS)
