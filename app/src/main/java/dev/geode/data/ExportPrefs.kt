package dev.geode.data

import android.content.SharedPreferences
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio

data class ExportDefaults(
    val quality: ExportQuality = ExportQuality.FHD1080,
    val fps: Int = 60,
    val ratio: ExportRatio = ExportRatio.R16_9,
    val loopSafe: Boolean = false,
)

internal fun exportQualityLabel(quality: ExportQuality): String =
    when (quality) {
        ExportQuality.HD720 -> "720p"
        ExportQuality.FHD1080 -> "1080p"
        ExportQuality.UHD4K -> "4K"
    }

class ExportPrefsStore(
    private val prefs: SharedPreferences,
) {
    fun load(): ExportDefaults {
        val d = ExportDefaults()
        return ExportDefaults(
            quality =
                runCatching { ExportQuality.valueOf(prefs.getString(KEY_QUALITY, null) ?: d.quality.name) }
                    .getOrDefault(d.quality),
            fps = prefs.getInt(KEY_FPS, d.fps).let { if (it == 30) 30 else 60 },
            ratio =
                runCatching { ExportRatio.valueOf(prefs.getString(KEY_RATIO, null) ?: d.ratio.name) }
                    .getOrDefault(d.ratio),
            loopSafe = prefs.getBoolean(KEY_LOOP, d.loopSafe),
        )
    }

    fun save(d: ExportDefaults) {
        prefs
            .edit()
            .putString(KEY_QUALITY, d.quality.name)
            .putInt(KEY_FPS, d.fps)
            .putString(KEY_RATIO, d.ratio.name)
            .putBoolean(KEY_LOOP, d.loopSafe)
            .apply()
    }

    private companion object {
        const val KEY_QUALITY = "export_quality"
        const val KEY_FPS = "export_fps"
        const val KEY_RATIO = "export_ratio"
        const val KEY_LOOP = "export_loop"
    }
}
