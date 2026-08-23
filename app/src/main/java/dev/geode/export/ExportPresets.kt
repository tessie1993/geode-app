package dev.geode.export

data class ExportPreset(
    val name: String,
    val quality: ExportQuality,
    val ratio: ExportRatio,
    val fps: Int,
    val loopSafe: Boolean,
)

object ExportPresets {
    val ALL: List<ExportPreset> =
        listOf(
            ExportPreset("TikTok", ExportQuality.FHD1080, ExportRatio.R9_16, fps = 30, loopSafe = true),
            ExportPreset("Shorts", ExportQuality.FHD1080, ExportRatio.R9_16, fps = 60, loopSafe = true),
            ExportPreset("Feed", ExportQuality.FHD1080, ExportRatio.R4_5, fps = 30, loopSafe = true),
            ExportPreset("Square", ExportQuality.FHD1080, ExportRatio.R1_1, fps = 30, loopSafe = true),
            ExportPreset("YouTube", ExportQuality.FHD1080, ExportRatio.R16_9, fps = 60, loopSafe = false),
            ExportPreset("Master", ExportQuality.UHD4K, ExportRatio.R16_9, fps = 60, loopSafe = false),
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
