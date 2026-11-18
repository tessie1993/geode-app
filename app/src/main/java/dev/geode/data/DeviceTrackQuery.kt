package dev.geode.data

import android.content.ContentUris
import android.content.Context
import android.provider.MediaStore
import dev.geode.engine.bridge.LibraryNative
import dev.geode.geodeContainer
import dev.geode.model.DeviceTrack

/** One MediaStore read: the tracks kept, and how many the minimum length left out. */
data class DeviceTrackScan(
    val tracks: List<DeviceTrack>,
    val hiddenShort: Int,
)

/**
 * The music MediaStore already indexes, read the same way for the library screen, voice search and
 * the Android Auto tree.
 *
 * Tracks shorter than the user's minimum ([LibraryPrefsStore.minTrackSeconds], 30 s unless
 * changed in Settings › Library folders) are left out here, once, so every caller agrees on what
 * the library holds. The rule itself is native's (`core/library/TrackRules`, `minTrackKeeps`).
 */
object DeviceTrackQuery {
    fun query(context: Context): List<DeviceTrack> =
        query(context, LibraryPrefsStore(context.geodeContainer.prefsFiles.library).minTrackSeconds())

    fun query(
        context: Context,
        minTrackSeconds: Int,
    ): List<DeviceTrack> = scan(context, minTrackSeconds).tracks

    fun scan(
        context: Context,
        minTrackSeconds: Int,
    ): DeviceTrackScan {
        val permission = android.Manifest.permission.READ_MEDIA_AUDIO
        val granted =
            androidx.core.content.ContextCompat
                .checkSelfPermission(context, permission) ==
                android.content.pm.PackageManager.PERMISSION_GRANTED
        if (!granted) return DeviceTrackScan(emptyList(), 0)
        // The rows as the cursor holds them; native keeps or hides each and words its tags (`geode_track_device_rows`).
        val ids = ArrayList<Long>()
        val added = ArrayList<Long>()
        val durations = ArrayList<Long>()
        val cells = ArrayList<String?>()
        val proj =
            arrayOf(
                MediaStore.Audio.Media._ID,
                MediaStore.Audio.Media.TITLE,
                MediaStore.Audio.Media.ARTIST,
                MediaStore.Audio.Media.ALBUM,
                MediaStore.Audio.Media.DURATION,
                MediaStore.Audio.Media.DATA,
                MediaStore.Audio.Media.DATE_ADDED,
            )
        runCatching {
            context.contentResolver
                .query(
                    MediaStore.Audio.Media.EXTERNAL_CONTENT_URI,
                    proj,
                    "${MediaStore.Audio.Media.IS_MUSIC} != 0",
                    null,
                    "${MediaStore.Audio.Media.TITLE} COLLATE NOCASE ASC",
                )?.use { c ->
                    val id = c.getColumnIndexOrThrow(MediaStore.Audio.Media._ID)
                    val ti = c.getColumnIndexOrThrow(MediaStore.Audio.Media.TITLE)
                    val ar = c.getColumnIndexOrThrow(MediaStore.Audio.Media.ARTIST)
                    val al = c.getColumnIndexOrThrow(MediaStore.Audio.Media.ALBUM)
                    val du = c.getColumnIndexOrThrow(MediaStore.Audio.Media.DURATION)
                    val da = c.getColumnIndexOrThrow(MediaStore.Audio.Media.DATA)
                    val ad = c.getColumnIndexOrThrow(MediaStore.Audio.Media.DATE_ADDED)
                    while (c.moveToNext()) {
                        ids += c.getLong(id)
                        added += c.getLong(ad)
                        durations += c.getLong(du)
                        cells += c.getString(ti)
                        cells += c.getString(ar)
                        cells += c.getString(al)
                        cells += c.getString(da)
                    }
                }
        }
        val records =
            TrackWire.records(
                LibraryNative.trackDeviceRows(
                    cells.toTypedArray(),
                    durations.toLongArray(),
                    minTrackSeconds,
                    arrayOf(
                        context.getString(dev.geode.R.string.library_unknown_title),
                        context.getString(dev.geode.R.string.subtitle_unknown_artist),
                        context.getString(dev.geode.R.string.library_unknown_album),
                    ),
                ),
            )
        // A record per kept row (index, title, artist, album, folder), then the count left out.
        val kept = records.dropLast(1).filter { it.size >= DEVICE_ROW_FIELDS - 1 }
        val out =
            kept.map { r ->
                val at = r[0].toInt()
                DeviceTrack(
                    uri = ContentUris.withAppendedId(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, ids[at]).toString(),
                    title = r[1],
                    artist = r[2],
                    album = r[3],
                    folder = r.getOrElse(4) { "" },
                    durationMs = durations[at],
                    addedSec = added[at],
                )
            }
        return DeviceTrackScan(out, records.lastOrNull()?.firstOrNull()?.toIntOrNull() ?: 0)
    }

    private const val DEVICE_ROW_FIELDS = 5
}
