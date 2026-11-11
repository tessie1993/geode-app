package dev.geode.data

import android.content.SharedPreferences
import dev.geode.export.ExportCodec
import dev.geode.export.ExportPreset
import dev.geode.export.ExportPresets
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio
import dev.geode.export.LoudnessTarget

/**
 * The settings an export is encoded with. One copy lives in Settings › Export per [ExportDestination]
 * ([ExportPrefsStore.loadFor]); a new Studio project takes a copy of its destination's and owns it
 * from then on (`editor/StudioProject.export`).
 *
 * [fps] defaults to 30, the best a free user may render, so a first free render never meets the
 * paywall; 60 is one tap away for Pro.
 */
data class ExportDefaults(
    val quality: ExportQuality = ExportQuality.FHD1080,
    val fps: Int = DEFAULT_EXPORT_FPS,
    val ratio: ExportRatio = ExportRatio.R16_9,
    val loopSafe: Boolean = false,
    val codec: ExportCodec = ExportCodec.H264,
    val loudnessTargetId: String = LoudnessTarget.LeaveAsIs.id,
    val saveTo: SaveTarget = SaveTarget.VideosLibrary,
)

internal const val DEFAULT_EXPORT_FPS: Int = 30

/**
 * Where a render is written. [VideosLibrary] is MediaStore's Movies/BeatRealms, which needs no
 * permission; [Folder] is a document tree the user picked (and granted persistently).
 */
sealed interface SaveTarget {
    data object VideosLibrary : SaveTarget

    data class Folder(
        val treeUri: String,
    ) : SaveTarget

    /** One string for a preference or a JSON field: "library", or the tree URI itself. */
    fun encode(): String =
        when (this) {
            VideosLibrary -> LIBRARY
            is Folder -> treeUri
        }

    companion object {
        private const val LIBRARY = "library"

        fun decode(raw: String?): SaveTarget = if (raw.isNullOrBlank() || raw == LIBRARY) VideosLibrary else Folder(raw)
    }
}

/**
 * Where a video is meant to go. Each destination has its own export defaults in Settings › Export,
 * which start from the platform's [preset] (or plain [ExportDefaults] for [CUSTOM]) and are then the
 * user's to change. A new project copies its destination's defaults; see `editor/StudioProject.create`.
 *
 * Declared in [ExportPresets.ALL]'s order, which [preset] relies on.
 */
enum class ExportDestination {
    TIKTOK,
    SHORTS,
    FEED,
    SQUARE,
    YOUTUBE,
    MASTER,
    CUSTOM,
    ;

    /** The platform preset this destination starts from; null for [CUSTOM]. */
    val preset: ExportPreset? get() = ExportPresets.ALL.getOrNull(ordinal).takeIf { this != CUSTOM }

    /** What Settings › Export shows for this destination before the user has changed anything. */
    fun factoryDefaults(): ExportDefaults = preset?.let { ExportDefaults().filledFrom(it) } ?: ExportDefaults()

    companion object {
        fun byName(name: String?): ExportDestination = entries.firstOrNull { it.name == name } ?: CUSTOM
    }
}

/**
 * [preset]'s quality, frame rate, aspect and looping written over these options, once. A platform
 * is a shortcut to a combination, not a mode: nothing remembers which one was used, so the next
 * change the user makes is simply the new value, and codec and loudness are never touched.
 */
fun ExportDefaults.filledFrom(preset: ExportPreset): ExportDefaults =
    copy(quality = preset.quality, ratio = preset.ratio, fps = preset.fps, loopSafe = preset.loopSafe)

// Kept in sync with the encoder ladder's 24..60 clamp (core/output/EncoderLadder): these are the frame rates
// offered anywhere in the UI, so nothing outside this set is ever persisted or requested.
internal val EXPORT_FPS_OPTIONS: List<Int> = listOf(24, 25, 30, 50, 60)

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

/**
 * Export settings in the general prefs.
 *
 * [load] and [save] are the Render page's working options (the unprefixed keys, as before).
 * [loadFor] and [saveFor] are Settings › Export's per-destination defaults, one key set per
 * [ExportDestination], which new projects copy. A destination never saved reads as its
 * [ExportDestination.factoryDefaults].
 */
class ExportPrefsStore(
    private val prefs: SharedPreferences,
) {
    fun load(): ExportDefaults = read(prefix = "", fallback = ExportDefaults())

    fun save(d: ExportDefaults) = write(prefix = "", d)

    fun loadFor(destination: ExportDestination): ExportDefaults = read(prefixOf(destination), destination.factoryDefaults())

    fun saveFor(
        destination: ExportDestination,
        d: ExportDefaults,
    ) = write(prefixOf(destination), d)

    private fun read(
        prefix: String,
        fallback: ExportDefaults,
    ): ExportDefaults {
        val d = fallback
        return ExportDefaults(
            quality =
                runCatching { ExportQuality.valueOf(prefs.getString(prefix + KEY_QUALITY, null) ?: d.quality.name) }
                    .getOrDefault(d.quality),
            fps = prefs.getInt(prefix + KEY_FPS, d.fps).let { if (it in EXPORT_FPS_OPTIONS) it else d.fps },
            ratio =
                runCatching { ExportRatio.valueOf(prefs.getString(prefix + KEY_RATIO, null) ?: d.ratio.name) }
                    .getOrDefault(d.ratio),
            loopSafe = prefs.getBoolean(prefix + KEY_LOOP, d.loopSafe),
            codec =
                runCatching { ExportCodec.valueOf(prefs.getString(prefix + KEY_CODEC, null) ?: d.codec.name) }
                    .getOrDefault(d.codec),
            loudnessTargetId = prefs.getString(prefix + KEY_LOUDNESS, null) ?: d.loudnessTargetId,
            saveTo = prefs.getString(prefix + KEY_SAVE_TO, null)?.let(SaveTarget::decode) ?: d.saveTo,
        )
    }

    private fun write(
        prefix: String,
        d: ExportDefaults,
    ) {
        prefs
            .edit()
            .putString(prefix + KEY_QUALITY, d.quality.name)
            .putInt(prefix + KEY_FPS, d.fps)
            .putString(prefix + KEY_RATIO, d.ratio.name)
            .putBoolean(prefix + KEY_LOOP, d.loopSafe)
            .putString(prefix + KEY_CODEC, d.codec.name)
            .putString(prefix + KEY_LOUDNESS, d.loudnessTargetId)
            .putString(prefix + KEY_SAVE_TO, d.saveTo.encode())
            .apply()
    }

    private fun prefixOf(destination: ExportDestination): String = "dest_${destination.name.lowercase()}_"

    private companion object {
        const val KEY_QUALITY = "export_quality"
        const val KEY_FPS = "export_fps"
        const val KEY_RATIO = "export_ratio"
        const val KEY_LOOP = "export_loop"
        const val KEY_CODEC = "export_codec"
        const val KEY_LOUDNESS = "export_loudness_target"
        const val KEY_SAVE_TO = "export_save_to"
        val FIELDS = listOf(KEY_QUALITY, KEY_FPS, KEY_RATIO, KEY_LOOP, KEY_CODEC, KEY_LOUDNESS, KEY_SAVE_TO)
    }
}
