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

/** Result of [NativeTags.write], distinguishing a deniable consent gap from a hard failure. */
sealed class TagWriteOutcome {
    /** The tag was written to the file. */
    data object Written : TagWriteOutcome()

    /**
     * The descriptor open was refused by scoped storage; a [android.provider.MediaStore.createWriteRequest]
     * may unblock it.
     */
    data object NeedsConsent : TagWriteOutcome()

    /** The descriptor could not be opened for a reason consent will not fix (missing file, read-only mount, ...). */
    data object Refused : TagWriteOutcome()

    /** The descriptor opened but the native writer rejected the format or found nothing to change. */
    data object Unsupported : TagWriteOutcome()
}

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
    ): TagWriteOutcome {
        val fd =
            when (val opened = writableFd(resolver, uri)) {
                is FdOpen -> opened.fd
                is FdFailed -> return opened.outcome
            }
        val texts =
            arrayOf(edit.title, edit.artist, edit.album, edit.albumArtist, edit.genre, edit.comment)
                .map { it.toByteArray(Charsets.UTF_8) }
                .toTypedArray()
        return if (GeodeNative.tagsWrite(fd, texts, edit.year, edit.track)) {
            TagWriteOutcome.Written
        } else {
            TagWriteOutcome.Unsupported
        }
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

    private sealed class FdResult

    private data class FdOpen(
        val fd: Int,
    ) : FdResult()

    private data class FdFailed(
        val outcome: TagWriteOutcome,
    ) : FdResult()

    // Scoped storage throws SecurityException (or, on API 29+, its RecoverableSecurityException
    // subclass) when the app never created the file and MediaStore.createWriteRequest consent is
    // required; any other failure to open is not something consent can fix.
    private fun writableFd(
        resolver: ContentResolver,
        uri: Uri,
    ): FdResult =
        try {
            val fd = resolver.openFileDescriptor(uri, "rw")?.detachFd()
            if (fd == null) {
                RingLog.note(TAG, "openFileDescriptor(rw) returned null")
                FdFailed(TagWriteOutcome.Refused)
            } else {
                FdOpen(fd)
            }
        } catch (e: SecurityException) {
            RingLog.note(TAG, "openFileDescriptor(rw) needs consent", e)
            FdFailed(TagWriteOutcome.NeedsConsent)
        } catch (e: FileNotFoundException) {
            RingLog.note(TAG, "openFileDescriptor(rw) failed", e)
            FdFailed(TagWriteOutcome.Refused)
        }
}
