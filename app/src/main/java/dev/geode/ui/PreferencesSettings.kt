package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.model.WatermarkChoice
import dev.geode.ui.pro.PaywallViewModel

/**
 * Settings › Preferences › Watermark: where the wordmark goes on a video (tiled across the frame,
 * a corner, or an end card), and for Pro whether it goes on at all. Free and trial videos always
 * carry it, so their switch is not shown. The page only stores the choice; every render pins it
 * when it starts, and where the mark lands and what an end card becomes on a loop or a recording is
 * native (`core/viz/frame/ExportPassMath`).
 */
@Composable
internal fun WatermarkRows(settings: SettingsViewModel) {
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val choice by settings.watermark.collectAsStateWithLifecycle()
    val required = gate.watermarksExports
    SettingsHint(stringResource(if (required) R.string.watermark_hint_free else R.string.watermark_hint_pro))
    if (!required) {
        SettingsSwitchRow(
            title = stringResource(R.string.watermark_pro_adds),
            checked = choice.proAdds,
            onCheckedChange = { on -> settings.updateWatermark { it.copy(proAdds = on) } },
        )
    }
    val marked = required || choice.proAdds
    SettingsChoiceRow(
        title = stringResource(R.string.watermark_placement),
        options = WatermarkChoice.PLACEMENTS,
        selected = choice.placement,
        label = { stringResource(placementLabel(it)) },
        onSelect = { placement -> settings.updateWatermark { it.copy(placement = placement) } },
        enabled = marked,
    )
    if (choice.placement == WatermarkChoice.CORNER) {
        SettingsChoiceRow(
            title = stringResource(R.string.watermark_corner),
            options = WatermarkChoice.CORNERS,
            selected = choice.corner,
            label = { stringResource(cornerLabel(it)) },
            onSelect = { corner -> settings.updateWatermark { it.copy(corner = corner) } },
            enabled = marked,
        )
    }
    if (choice.placement == WatermarkChoice.END_CARD) SettingsHint(stringResource(R.string.watermark_end_card_hint))
}

private fun placementLabel(placement: Int): Int =
    when (placement) {
        WatermarkChoice.CORNER -> R.string.watermark_placement_corner
        WatermarkChoice.END_CARD -> R.string.watermark_placement_end_card
        else -> R.string.watermark_placement_tiled
    }

private fun cornerLabel(corner: Int): Int =
    when (corner) {
        WatermarkChoice.TOP_LEFT -> R.string.watermark_corner_top_left
        WatermarkChoice.TOP_RIGHT -> R.string.watermark_corner_top_right
        WatermarkChoice.BOTTOM_LEFT -> R.string.watermark_corner_bottom_left
        else -> R.string.watermark_corner_bottom_right
    }
