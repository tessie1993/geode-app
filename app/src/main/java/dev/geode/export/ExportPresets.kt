package dev.geode.export

import androidx.annotation.StringRes
import dev.geode.R

/**
 * One platform's usual combination of options. It only fills the options in
 * ([dev.geode.data.filledFrom]); the user can pick any combination with or without one.
 */
data class ExportPreset(
    @param:StringRes val nameRes: Int,
    val quality: ExportQuality,
    val ratio: ExportRatio,
    val fps: Int,
    val loopSafe: Boolean,
)

object ExportPresets {
    val ALL: List<ExportPreset> =
        listOf(
            ExportPreset(R.string.export_preset_tiktok, ExportQuality.FHD1080, ExportRatio.R9_16, fps = 30, loopSafe = true),
            ExportPreset(R.string.export_preset_shorts, ExportQuality.FHD1080, ExportRatio.R9_16, fps = 60, loopSafe = true),
            ExportPreset(R.string.export_preset_feed, ExportQuality.FHD1080, ExportRatio.R4_5, fps = 30, loopSafe = true),
            ExportPreset(R.string.export_preset_square, ExportQuality.FHD1080, ExportRatio.R1_1, fps = 30, loopSafe = true),
            ExportPreset(R.string.export_preset_youtube, ExportQuality.FHD1080, ExportRatio.R16_9, fps = 60, loopSafe = false),
            ExportPreset(R.string.export_preset_master, ExportQuality.UHD4K, ExportRatio.R16_9, fps = 60, loopSafe = false),
        )
}
