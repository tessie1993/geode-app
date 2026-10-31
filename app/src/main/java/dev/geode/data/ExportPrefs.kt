package dev.geode.data

import android.content.SharedPreferences
import dev.geode.export.ExportCodec
import dev.geode.export.ExportPreset
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio
import dev.geode.export.LoudnessTarget

data class ExportDefaults(
    val quality: ExportQuality = ExportQuality.FHD1080,
    val fps: Int = 60,
    val ratio: ExportRatio = ExportRatio.R16_9,
    val loopSafe: Boolean = false,
    val codec: ExportCodec = ExportCodec.H264,
    val loudnessTargetId: String = LoudnessTarget.LeaveAsIs.id,
)

/**
 * [preset]'s quality, frame rate, aspect and looping written over these options, once. A platform
 * is a shortcut to a combination, not a mode: nothing remembers which one was used, so the next
 * change the user makes is simply the new value, and codec and loudness are never touched.
 */
fun ExportDefaults.filledFrom(preset: ExportPreset): ExportDefaults =
    copy(quality = preset.quality, ratio = preset.ratio, fps = preset.fps, loopSafe = preset.loopSafe)

// Kept in sync with VideoExporter's `requestedFps.coerceIn(24, 60)`: these are the frame rates
// offered anywhere in the UI, so nothing outside this set is ever persisted or requested.
internal val EXPORT_FPS_OPTIONS: List<Int> = listOf(24, 25, 30, 50, 60)

/**
 * The rate an export retries at when the encoder refuses the requested one: half of a 50 or 60
 * request, so the retry stays in the request's cadence family (50 -> 25, not 30), and the request
 * itself at 30 and below. A fixed 30, as before, retried a 24 or 25 fps request at a higher rate.
 */
internal fun fallbackExportFps(requested: Int): Int = if (requested > FALLBACK_CEILING_FPS) requested / 2 else requested

private const val FALLBACK_CEILING_FPS = 30

internal fun exportCodecLabel(codec: ExportCodec): String =
    when (codec) {
        ExportCodec.H264 -> "H.264"
        ExportCodec.HEVC -> "HEVC"
    }

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
            fps = prefs.getInt(KEY_FPS, d.fps).let { if (it in EXPORT_FPS_OPTIONS) it else d.fps },
            ratio =
                runCatching { ExportRatio.valueOf(prefs.getString(KEY_RATIO, null) ?: d.ratio.name) }
                    .getOrDefault(d.ratio),
            loopSafe = prefs.getBoolean(KEY_LOOP, d.loopSafe),
            codec =
                runCatching { ExportCodec.valueOf(prefs.getString(KEY_CODEC, null) ?: d.codec.name) }
                    .getOrDefault(d.codec),
            loudnessTargetId = prefs.getString(KEY_LOUDNESS, null) ?: d.loudnessTargetId,
        )
    }

    fun save(d: ExportDefaults) {
        prefs
            .edit()
            .putString(KEY_QUALITY, d.quality.name)
            .putInt(KEY_FPS, d.fps)
            .putString(KEY_RATIO, d.ratio.name)
            .putBoolean(KEY_LOOP, d.loopSafe)
            .putString(KEY_CODEC, d.codec.name)
            .putString(KEY_LOUDNESS, d.loudnessTargetId)
            .apply()
    }

    private companion object {
        const val KEY_QUALITY = "export_quality"
        const val KEY_FPS = "export_fps"
        const val KEY_RATIO = "export_ratio"
        const val KEY_LOOP = "export_loop"
        const val KEY_CODEC = "export_codec"
        const val KEY_LOUDNESS = "export_loudness_target"
    }
}
