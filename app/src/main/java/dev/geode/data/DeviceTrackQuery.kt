package dev.geode.data

import android.content.ContentUris
import android.content.Context
import android.provider.MediaStore
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
 * the library holds. The rule itself is [MinTrackLength.keeps].
 */
object DeviceTrackQuery {
    fun query(context: Context): List<DeviceTrack> = query(context, LibraryPrefsStore(GeodePrefsFiles(context).library).minTrackSeconds())

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
        val out = mutableListOf<DeviceTrack>()
        var hidden = 0
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
                        val durationMs = c.getLong(du)
                        if (!MinTrackLength.keeps(durationMs, minTrackSeconds)) {
                            hidden++
                            continue
                        }
                        val uri = ContentUris.withAppendedId(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, c.getLong(id))
                        val path = c.getString(da).orEmpty()
                        out +=
                            DeviceTrack(
                                uri = uri.toString(),
                                title = tagOr(c.getString(ti), context.getString(dev.geode.R.string.library_unknown_title)),
                                artist = tagOr(c.getString(ar), context.getString(dev.geode.R.string.subtitle_unknown_artist)),
                                album = tagOr(c.getString(al), context.getString(dev.geode.R.string.library_unknown_album)),
                                folder = path.substringBeforeLast('/', ""),
                                durationMs = durationMs,
                                addedSec = c.getLong(ad),
                            )
                    }
                }
        }
        return DeviceTrackScan(out, hidden)
    }

    /**
     * A tag as the library shows it: MediaStore writes [MediaStore.UNKNOWN_STRING] ("<unknown>")
     * for an untagged file's artist and album, which the list and the mini player showed raw, so
     * that, a missing and a blank tag all read as the localized [fallback].
     */
    fun tagOr(
        value: String?,
        fallback: String,
    ): String = if (value.isNullOrBlank() || value == MediaStore.UNKNOWN_STRING) fallback else value
}
