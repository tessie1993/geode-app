package dev.geode.audio

import android.content.Context
import android.content.res.AssetFileDescriptor
import android.net.Uri
import dev.geode.engine.bridge.GeodeNative

/**
 * An AIFF or AIFF-C file read as 16-bit PCM, for what MediaExtractor does not read (the export's
 * [dev.geode.render.offscreen.TrackAudio] and the soundtrack transcoder). A handle: the header walk and
 * the sample conversion are native (`core/audio/ingest/AiffReader`, pinned to this class's old Kotlin by
 * `core/tests/track_feed_test.cpp`); this only opens the file's descriptor and hands it over.
 *
 * One thread at a time. [close] frees it.
 */
class AiffPcm private constructor(
    opened: Long,
) {
    /** The native reader; 0 once closed, which every native call takes as the end. */
    internal var handle: Long = opened
        private set

    private val info = LongArray(INFO_LONGS).also { GeodeNative.aiffInfo(opened, it) }

    val channels: Int get() = info[0].toInt()

    val sampleRate: Int get() = info[1].toInt()

    val progress: Float get() = GeodeNative.aiffProgress(handle)

    /** Up to [out]'s size in interleaved samples, whole frames while the file has them; -1 at the end. */
    fun read(out: ShortArray): Int = GeodeNative.aiffReadS16(handle, out)

    /** Idempotent. */
    fun close() {
        val owned = handle
        handle = 0L
        if (owned != 0L) GeodeNative.aiffClose(owned)
    }

    companion object {
        private const val INFO_LONGS = 3

        /** Null when [uri] is not an AIFF native reads; throws as `openInputStream` would when it cannot open. */
        fun open(
            context: Context,
            uri: Uri,
        ): AiffPcm? {
            val afd = context.contentResolver.openAssetFileDescriptor(uri, "r") ?: return null
            // What openInputStream reads: the declared span, or the whole descriptor when none is declared.
            val length = afd.declaredLength
            val start = if (length == AssetFileDescriptor.UNKNOWN_LENGTH) 0L else afd.startOffset
            val handle = GeodeNative.aiffOpenFd(afd.parcelFileDescriptor.detachFd(), start, length)
            return if (handle != 0L) AiffPcm(handle) else null
        }
    }
}
