package dev.geode.data

import android.content.SharedPreferences
import androidx.core.content.edit

/**
 * The shortest track the library lists, in seconds. Phones keep ringtones, notification sounds,
 * voice notes and WhatsApp clips in folders MediaStore calls music, and a library that opens on a
 * wall of three-second blips reads as broken. 30 s by default; 0 lists everything.
 *
 * A track whose length MediaStore does not know (0 or negative) is always kept: hiding music
 * because an indexer missed a tag would be worse than showing a stray clip.
 */
object MinTrackLength {
    const val DEFAULT_SECONDS = 30

    /** What Settings › Library folders offers. 0 is "Off". */
    val CHOICES: List<Int> = listOf(0, 10, 30, 60, 120)

    fun keeps(
        durationMs: Long,
        minSeconds: Int,
    ): Boolean = minSeconds <= 0 || durationMs <= 0L || durationMs >= minSeconds * MS_PER_SECOND

    private const val MS_PER_SECOND = 1_000L
}

/**
 * The library's own choices, in `geode-library` beside the music folders' roots. The roots are
 * device-local (`BackupFormat.deviceLocalKeys`); [minTrackSeconds] is a setting and travels with
 * Settings in a backup.
 */
class LibraryPrefsStore(
    private val prefs: SharedPreferences,
) {
    fun minTrackSeconds(): Int = prefs.getInt(KEY_MIN_TRACK_SECONDS, MinTrackLength.DEFAULT_SECONDS).coerceAtLeast(0)

    fun setMinTrackSeconds(seconds: Int) = prefs.edit { putInt(KEY_MIN_TRACK_SECONDS, seconds.coerceAtLeast(0)) }

    companion object {
        const val KEY_MIN_TRACK_SECONDS = "min_track_seconds"
    }
}
