package dev.geode.ui

import android.content.Intent
import android.net.Uri
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
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.ui.opaline.creative.CreativeButton
import dev.geode.ui.opaline.creative.CreativeSlider
import dev.geode.viz.ArtTitleOptions
import dev.geode.viz.LyricOptions
import dev.geode.viz.LyricPosition
import dev.geode.viz.LyricSize
import dev.geode.viz.OverlayPosition
import dev.geode.viz.OverlaySize
import dev.geode.viz.WatermarkCorner
import dev.geode.viz.WatermarkOptions
import dev.geode.ui.opaline.creative.CreativeToggle as Switch

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

private fun watermarkCornerLabel(corner: WatermarkCorner) =
    when (corner) {
        WatermarkCorner.TOP_LEFT -> R.string.overlay_watermark_corner_top_left
        WatermarkCorner.TOP_RIGHT -> R.string.overlay_watermark_corner_top_right
        WatermarkCorner.BOTTOM_LEFT -> R.string.overlay_watermark_corner_bottom_left
        WatermarkCorner.BOTTOM_RIGHT -> R.string.overlay_watermark_corner_bottom_right
    }

private fun lyricPositionLabel(position: LyricPosition) =
    when (position) {
        LyricPosition.TOP -> R.string.overlay_lyric_position_top
        LyricPosition.CENTER -> R.string.overlay_lyric_position_center
        LyricPosition.BOTTOM -> R.string.overlay_lyric_position_bottom
    }

private fun lyricSizeLabel(size: LyricSize) =
    when (size) {
        LyricSize.SMALL -> R.string.overlay_size_small
        LyricSize.MEDIUM -> R.string.overlay_size_medium
        LyricSize.LARGE -> R.string.overlay_size_large
    }

/**
 * Toggles and choices for the cover-art/title, synced-lyric, and watermark overlays drawn into
 * the visualizer and its exports.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun LayersSheet(
    options: ArtTitleOptions,
    onOptionsChange: (ArtTitleOptions) -> Unit,
    watermarkOptions: WatermarkOptions,
    onWatermarkOptionsChange: (WatermarkOptions) -> Unit,
    onPickWatermarkImage: (Uri) -> Unit,
    onClearWatermarkImage: () -> Unit,
    lyricOptions: LyricOptions,
    onLyricOptionsChange: (LyricOptions) -> Unit,
    onDismiss: () -> Unit,
) {
    OpalineContextSheet(onDismiss = onDismiss) {
        Column(Modifier.verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
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
                    CreativeButton(
                        compact = true,
                        filled = position == options.position,
                        onClick = { onOptionsChange(options.copy(position = position)) },
                    ) { Text(stringResource(positionLabel(position))) }
                }
            }

            Text(stringResource(R.string.overlay_size_label), style = MaterialTheme.typography.labelLarge)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                OverlaySize.entries.forEach { size ->
                    CreativeButton(
                        compact = true,
                        filled = size == options.size,
                        onClick = { onOptionsChange(options.copy(size = size)) },
                    ) { Text(stringResource(sizeLabel(size))) }
                }
            }

            HorizontalDivider()
            Text(stringResource(R.string.overlay_lyric_sheet_title), style = MaterialTheme.typography.titleMedium)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(stringResource(R.string.overlay_lyric_enable))
                Switch(
                    checked = lyricOptions.enabled,
                    onCheckedChange = { onLyricOptionsChange(lyricOptions.copy(enabled = it)) },
                )
            }

            Text(stringResource(R.string.overlay_position_label), style = MaterialTheme.typography.labelLarge)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                LyricPosition.entries.forEach { position ->
                    CreativeButton(
                        compact = true,
                        filled = position == lyricOptions.position,
                        onClick = { onLyricOptionsChange(lyricOptions.copy(position = position)) },
                    ) { Text(stringResource(lyricPositionLabel(position))) }
                }
            }

            Text(stringResource(R.string.overlay_size_label), style = MaterialTheme.typography.labelLarge)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                LyricSize.entries.forEach { size ->
                    CreativeButton(
                        compact = true,
                        filled = size == lyricOptions.size,
                        onClick = { onLyricOptionsChange(lyricOptions.copy(size = size)) },
                    ) { Text(stringResource(lyricSizeLabel(size))) }
                }
            }

            HorizontalDivider()
            WatermarkSection(watermarkOptions, onWatermarkOptionsChange, onPickWatermarkImage, onClearWatermarkImage)
        }
    }
}

/** Logo/watermark row group: pick or clear an image, then its corner, size and opacity. */
@Composable
private fun WatermarkSection(
    options: WatermarkOptions,
    onOptionsChange: (WatermarkOptions) -> Unit,
    onPickImage: (Uri) -> Unit,
    onClearImage: () -> Unit,
) {
    val context = LocalContext.current
    val picker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                runCatching {
                    context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
                onPickImage(uri)
            }
        }

    Text(stringResource(R.string.overlay_watermark_section), style = MaterialTheme.typography.titleMedium)

    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        CreativeButton(compact = true, filled = false, onClick = { picker.launch(arrayOf("image/*")) }) {
            Text(stringResource(R.string.overlay_watermark_pick))
        }
        if (options.uri != null) {
            CreativeButton(compact = true, filled = false, onClick = onClearImage) {
                Text(stringResource(R.string.overlay_watermark_clear))
            }
        }
    }
    if (options.uri == null) {
        Text(
            stringResource(R.string.overlay_watermark_none),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }

    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(stringResource(R.string.overlay_watermark_enable))
        Switch(
            checked = options.enabled,
            enabled = options.uri != null,
            onCheckedChange = { onOptionsChange(options.copy(enabled = it)) },
        )
    }

    Text(stringResource(R.string.overlay_watermark_corner_label), style = MaterialTheme.typography.labelLarge)
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        WatermarkCorner.entries.forEach { corner ->
            CreativeButton(
                compact = true,
                filled = corner == options.corner,
                onClick = { onOptionsChange(options.copy(corner = corner)) },
            ) { Text(stringResource(watermarkCornerLabel(corner))) }
        }
    }

    Column {
        Text(
            stringResource(R.string.overlay_watermark_size_label, (options.sizeFraction * 100).toInt()),
            style = MaterialTheme.typography.labelLarge,
        )
        CreativeSlider(
            value = options.sizeFraction,
            onValueChange = { onOptionsChange(options.copy(sizeFraction = it)) },
            valueRange = WatermarkOptions.MIN_SIZE_FRACTION..WatermarkOptions.MAX_SIZE_FRACTION,
        )
    }

    Column {
        Text(
            stringResource(R.string.overlay_watermark_opacity_label, (options.opacity * 100).toInt()),
            style = MaterialTheme.typography.labelLarge,
        )
        CreativeSlider(
            value = options.opacity,
            onValueChange = { onOptionsChange(options.copy(opacity = it)) },
            valueRange = WatermarkOptions.MIN_OPACITY..WatermarkOptions.MAX_OPACITY,
        )
    }
}
