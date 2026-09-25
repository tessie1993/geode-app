package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
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
import dev.geode.ui.opaline.creative.CreativeButton
import dev.geode.ui.opaline.creative.CreativeSlider
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

    OpalineContextSheet(onDismiss = onDismiss) {
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 8.dp),
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
                CreativeButton(compact = true, filled = false, onClick = { picker.launch(arrayOf("image/*")) }) {
                    Text(stringResource(R.string.background_pick))
                }
                if (prefs.uri != null) {
                    CreativeButton(compact = true, filled = false, onClick = visualsViewModel::clearBackgroundImage) {
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
                CreativeSlider(
                    value = prefs.amount,
                    onValueChange = visualsViewModel::setBackgroundAmount,
                    valueRange = 0f..1f,
                )
            }
            Column {
                Text(stringResource(R.string.background_blur, prefs.blurRadius), style = MaterialTheme.typography.labelMedium)
                CreativeSlider(
                    value = prefs.blurRadius.toFloat(),
                    onValueChange = { visualsViewModel.setBackgroundBlurRadius(it.roundToInt()) },
                    valueRange = BackgroundPrefsStore.BLUR_RANGE.first.toFloat()..BackgroundPrefsStore.BLUR_RANGE.last.toFloat(),
                )
            }
            Column {
                Text(
                    stringResource(R.string.background_dim, (prefs.dim * 100).roundToInt()),
                    style = MaterialTheme.typography.labelMedium,
                )
                CreativeSlider(
                    value = prefs.dim,
                    onValueChange = visualsViewModel::setBackgroundDim,
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
    CreativeButton(compact = true, filled = current == option, onClick = { onSelect(option) }) {
        Text(stringResource(labelRes), style = MaterialTheme.typography.bodySmall)
    }
}
