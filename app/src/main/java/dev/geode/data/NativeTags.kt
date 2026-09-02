package dev.geode.data

import android.content.ContentResolver
import android.net.Uri
import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import java.io.FileNotFoundException

data class TrackTags(
    val title: String,
    val artist: String,
    val album: String,
    val albumArtist: String,
    val genre: String,
    val comment: String,
    val year: Int,
    val track: Int,
    val durationMs: Int,
    val artBytes: Int,
    val trackGainDb: Float?,
    val trackPeak: Float?,
    val albumGainDb: Float?,
    val albumPeak: Float?,
)

data class TrackTagEdit(
    val title: String,
    val artist: String,
    val album: String,
    val albumArtist: String,
    val genre: String,
    val comment: String,
    val year: Int,
    val track: Int,
)

/** TagLib behind [GeodeNative.tagsRead] and [GeodeNative.tagsWrite], fed by content URIs. */
object NativeTags {
    private const val TAG = "NativeTags"
    private const val TEXT_FIELDS = 6
    private const val TRACK_GAIN = 1
    private const val TRACK_PEAK = 2
    private const val ALBUM_GAIN = 4
    private const val ALBUM_PEAK = 8

    fun read(
        resolver: ContentResolver,
        uri: Uri,
    ): TrackTags? {
        val fd = detachedFd(resolver, uri, "r") ?: return null
        val texts = arrayOfNulls<ByteArray>(TEXT_FIELDS)
        val ints = IntArray(4)
        val gains = FloatArray(4)
        val mask = GeodeNative.tagsRead(fd, texts, ints, gains)
        if (mask < 0) return null

        fun text(index: Int): String = texts[index]?.toString(Charsets.UTF_8).orEmpty()

        fun gain(
            bit: Int,
            index: Int,
        ): Float? = gains[index].takeIf { mask and bit != 0 }
        return TrackTags(
            title = text(0),
            artist = text(1),
            album = text(2),
            albumArtist = text(3),
            genre = text(4),
            comment = text(5),
            year = ints[0],
            track = ints[1],
            durationMs = ints[2],
            artBytes = ints[3],
            trackGainDb = gain(TRACK_GAIN, 0),
            trackPeak = gain(TRACK_PEAK, 1),
            albumGainDb = gain(ALBUM_GAIN, 2),
            albumPeak = gain(ALBUM_PEAK, 3),
        )
    }

    fun write(
        resolver: ContentResolver,
        uri: Uri,
        edit: TrackTagEdit,
    ): Boolean {
        val fd = detachedFd(resolver, uri, "rw") ?: return false
        val texts =
            arrayOf(edit.title, edit.artist, edit.album, edit.albumArtist, edit.genre, edit.comment)
                .map { it.toByteArray(Charsets.UTF_8) }
                .toTypedArray()
        return GeodeNative.tagsWrite(fd, texts, edit.year, edit.track)
    }

    // TagLib wraps the descriptor in a FILE* and closes it, so the ParcelFileDescriptor must let go of it first.
    private fun detachedFd(
        resolver: ContentResolver,
        uri: Uri,
        mode: String,
    ): Int? =
        try {
            resolver.openFileDescriptor(uri, mode)?.detachFd()
        } catch (e: FileNotFoundException) {
            RingLog.note(TAG, "openFileDescriptor($mode) failed", e)
            null
        } catch (e: SecurityException) {
            RingLog.note(TAG, "openFileDescriptor($mode) refused", e)
            null
        }
}
