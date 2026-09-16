package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.viz.ArtTitleOptions
import dev.geode.viz.OverlayPosition
import dev.geode.viz.OverlaySize

private fun positionLabel(position: OverlayPosition) =
    when (position) {
        OverlayPosition.BOTTOM_LEFT -> R.string.overlay_position_bottom_left
        OverlayPosition.BOTTOM_CENTER -> R.string.overlay_position_bottom_center
        OverlayPosition.TOP_LEFT -> R.string.overlay_position_top_left
        OverlayPosition.TOP_RIGHT -> R.string.overlay_position_top_right
    }

private fun sizeLabel(size: OverlaySize) =
    when (size) {
        OverlaySize.SMALL -> R.string.overlay_size_small
        OverlaySize.MEDIUM -> R.string.overlay_size_medium
        OverlaySize.LARGE -> R.string.overlay_size_large
    }

/** Toggles and choices for the cover-art/title overlay drawn into the visualizer and its exports. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun LayersSheet(
    options: ArtTitleOptions,
    onOptionsChange: (ArtTitleOptions) -> Unit,
    onDismiss: () -> Unit,
) {
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(stringResource(R.string.overlay_sheet_title), style = MaterialTheme.typography.titleMedium)

            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(stringResource(R.string.overlay_enable))
                Switch(checked = options.enabled, onCheckedChange = { onOptionsChange(options.copy(enabled = it)) })
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(stringResource(R.string.overlay_show_artwork))
                Switch(
                    checked = options.showArtwork,
                    onCheckedChange = { onOptionsChange(options.copy(showArtwork = it)) },
                )
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(stringResource(R.string.overlay_show_text))
                Switch(checked = options.showText, onCheckedChange = { onOptionsChange(options.copy(showText = it)) })
            }

            Text(stringResource(R.string.overlay_position_label), style = MaterialTheme.typography.labelLarge)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                OverlayPosition.entries.forEach { position ->
                    CrystalButton(
                        compact = true,
                        filled = position == options.position,
                        onClick = { onOptionsChange(options.copy(position = position)) },
                    ) { Text(stringResource(positionLabel(position))) }
                }
            }

            Text(stringResource(R.string.overlay_size_label), style = MaterialTheme.typography.labelLarge)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                OverlaySize.entries.forEach { size ->
                    CrystalButton(
                        compact = true,
                        filled = size == options.size,
                        onClick = { onOptionsChange(options.copy(size = size)) },
                    ) { Text(stringResource(sizeLabel(size))) }
                }
            }
        }
    }
}
