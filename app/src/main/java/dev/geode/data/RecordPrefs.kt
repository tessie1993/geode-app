package dev.geode.data

import android.content.SharedPreferences
import androidx.core.content.edit
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative
import dev.geode.export.ExportQuality

/**
 * What a recorded take renders at, and how long it may run: Settings › Export › Record.
 *
 * [maxLengthSec] is one of the lengths native offers (`core/output/RecordPlan`), 0 for no limit: a
 * recording that reaches it stops and saves like a manual stop.
 */
data class RecordPrefs(
    val quality: ExportQuality,
    val fps: Int,
    val maxLengthSec: Int = 0,
) {
    /** [defaults] with the take's resolution and frame rate; ratio, codec, loudness and folder stay. */
    fun applyTo(defaults: ExportDefaults): ExportDefaults = defaults.copy(quality = quality, fps = fps)
}

/**
 * The three Record choices, in the general prefs beside the export defaults (so the manual backup
 * carries them like those).
 *
 * A choice never made reads as what a take did before these rows existed: the resolution and frame
 * rate of the Custom export defaults ([ExportPrefsStore.loadFor] of [ExportDestination.CUSTOM],
 * handed in as `fallback`), and no maximum length. So nothing changes for anyone until they pick,
 * and an untouched row keeps following the Custom defaults.
 *
 * Kotlin only stores the values. Which lengths exist and what a stored one snaps to is native
 * ([snap], `geode_record_snap_max_length`); the frame rates are the export's own [EXPORT_FPS_OPTIONS].
 */
class RecordPrefsStore(
    private val prefs: SharedPreferences,
    private val snap: (Int) -> Int = { GeodeNative.recordSnapMaxLength(it) },
) {
    fun load(fallback: ExportDefaults): RecordPrefs =
        RecordPrefs(
            quality =
                runCatching { ExportQuality.valueOf(prefs.getString(KEY_QUALITY, null) ?: fallback.quality.name) }
                    .getOrDefault(fallback.quality),
            fps = prefs.getInt(KEY_FPS, fallback.fps).let { if (it in EXPORT_FPS_OPTIONS) it else fallback.fps },
            maxLengthSec = maxLengthSec(),
        )

    /** The maximum length alone, for the recorder's tick; 0 is no limit. */
    fun maxLengthSec(): Int = prefs.getInt(KEY_MAX_SEC, 0).let { if (it > 0) snap(it) else 0 }

    fun save(record: RecordPrefs) =
        prefs.edit {
            putString(KEY_QUALITY, record.quality.name)
            putInt(KEY_FPS, record.fps)
            putInt(KEY_MAX_SEC, record.maxLengthSec)
        }

    companion object {
        private const val KEY_QUALITY = "record_quality"
        private const val KEY_FPS = "record_fps"
        private const val KEY_MAX_SEC = "record_max_sec"

        /** Every key this store writes. */
        val KEYS: Set<String> = setOf(KEY_QUALITY, KEY_FPS, KEY_MAX_SEC)

        internal val PREF_KEYS =
            prefKeys(GeodePrefsFiles.GENERAL) {
                portable(PrefType.STRING, KEY_QUALITY)
                portable(PrefType.INT, KEY_FPS, KEY_MAX_SEC)
            }
    }
}
