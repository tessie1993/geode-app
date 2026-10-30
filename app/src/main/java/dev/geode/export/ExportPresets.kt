package dev.geode.export

import androidx.annotation.StringRes
import dev.geode.R

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

    fun matching(
        quality: ExportQuality,
        ratio: ExportRatio,
        fps: Int,
        loopSafe: Boolean,
    ): ExportPreset? = ALL.firstOrNull { it.isFor(quality, ratio, fps, loopSafe) }

    fun indexMatching(
        quality: ExportQuality,
        ratio: ExportRatio,
        fps: Int,
        loopSafe: Boolean,
    ): Int = ALL.indexOfFirst { it.isFor(quality, ratio, fps, loopSafe) }

    private fun ExportPreset.isFor(
        quality: ExportQuality,
        ratio: ExportRatio,
        fps: Int,
        loopSafe: Boolean,
    ): Boolean = this.quality == quality && this.ratio == ratio && this.fps == fps && this.loopSafe == loopSafe
}
