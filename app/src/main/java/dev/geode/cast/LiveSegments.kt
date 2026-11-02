package dev.geode.cast

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.channels.WritableByteChannel
import java.util.Locale
import kotlin.math.ceil

/**
 * The live HLS stream the TV plays, built from the fragmented MP4 that `FragmentedMp4Muxer` writes.
 *
 * The muxer writes one continuous file: `ftyp` and `moov` once, then a `moof` and `mdat` pair per
 * fragment. HLS with fMP4 segments (CMAF) wants exactly that split up: the header as the init
 * segment (`#EXT-X-MAP`) and each `moof`+`mdat` as one media segment. So this is the channel the
 * muxer writes to, and it cuts the bytes at box boundaries as they arrive: nothing is parsed
 * beyond each top-level box's size and type.
 *
 * Only the newest [WINDOW] segments are kept, in memory: a live playlist only ever names the last
 * few, and the receiver starts near the live edge. Wall-clock time between two finished segments is
 * their `#EXTINF`; the muxer cuts at the encoder's keyframes, one a second ([CastEncoder]), so that
 * is close to the media time and the receiver tolerates the difference on a live stream.
 *
 * Written from the encoder's drain thread, read from the server's threads: every access to the
 * segments goes through the monitor.
 */
internal class LiveSegments(
    private val nowMs: () -> Long = System::currentTimeMillis,
) : WritableByteChannel {
    private class Segment(
        val sequence: Long,
        val bytes: ByteArray,
        val durationSec: Double,
    )

    private val pending = ByteArrayOutputStream()
    private val header = ByteArrayOutputStream()
    private var fragment: ByteArrayOutputStream? = null
    private val segments = ArrayDeque<Segment>()
    private var nextSequence = 0L
    private var lastCutMs = 0L
    private var open = true

    @Volatile
    private var init: ByteArray? = null

    override fun isOpen(): Boolean = open

    override fun close() {
        open = false
    }

    @Synchronized
    override fun write(src: ByteBuffer): Int {
        val count = src.remaining()
        val chunk = ByteArray(count)
        src.get(chunk)
        pending.write(chunk)
        drainBoxes()
        return count
    }

    /** Moves every complete top-level box out of [pending]. */
    private fun drainBoxes() {
        val bytes = pending.toByteArray()
        var at = 0
        while (bytes.size - at >= BOX_HEADER) {
            val size = boxSize(bytes, at)
            if (size < BOX_HEADER || bytes.size - at < size) break
            onBox(String(bytes, at + 4, 4, Charsets.US_ASCII), bytes, at, size.toInt())
            at += size.toInt()
        }
        pending.reset()
        pending.write(bytes, at, bytes.size - at)
    }

    private fun onBox(
        type: String,
        bytes: ByteArray,
        offset: Int,
        size: Int,
    ) {
        when (type) {
            "ftyp", "moov" -> {
                header.write(bytes, offset, size)
                if (type == "moov") {
                    init = header.toByteArray()
                    lastCutMs = nowMs()
                }
            }
            "moof" -> fragment = ByteArrayOutputStream().apply { write(bytes, offset, size) }
            "mdat" ->
                fragment?.let {
                    it.write(bytes, offset, size)
                    publish(it.toByteArray())
                    fragment = null
                }
            // styp, sidx, mfra (at close): nothing the playlist needs.
            else -> Unit
        }
    }

    private fun publish(bytes: ByteArray) {
        val now = nowMs()
        val duration = ((now - lastCutMs).coerceAtLeast(1L)) / MS_PER_SEC
        lastCutMs = now
        segments.addLast(Segment(nextSequence++, bytes, duration))
        while (segments.size > WINDOW) segments.removeFirst()
    }

    /** Milliseconds since the last segment was cut (or the header was written); 0 before that. */
    @Synchronized
    fun msSinceLastSegment(): Long = if (lastCutMs == 0L) 0L else nowMs() - lastCutMs

    /** The init segment, or null until the muxer has written its header. */
    fun initSegment(): ByteArray? = init

    @Synchronized
    fun segment(sequence: Long): ByteArray? = segments.firstOrNull { it.sequence == sequence }?.bytes

    /**
     * The live media playlist, or null until there is enough to start on: the receiver's player
     * starts [START_SEGMENTS] from the live edge, and a playlist shorter than that stalls it.
     */
    @Synchronized
    fun playlist(): String? {
        if (init == null || segments.size < START_SEGMENTS) return null
        val target =
            ceil(segments.maxOf { it.durationSec })
                .toInt()
                .coerceAtLeast(1)
        return buildString {
            append("#EXTM3U\n#EXT-X-VERSION:7\n")
            append("#EXT-X-TARGETDURATION:").append(target).append('\n')
            append("#EXT-X-MEDIA-SEQUENCE:").append(segments.first().sequence).append('\n')
            append("#EXT-X-MAP:URI=\"").append(INIT_NAME).append("\"\n")
            segments.forEach {
                append("#EXTINF:").append(String.format(Locale.ROOT, "%.3f", it.durationSec)).append(",\n")
                append(SEGMENT_PREFIX).append(it.sequence).append(SEGMENT_SUFFIX).append('\n')
            }
        }
    }

    companion object {
        const val PLAYLIST_NAME = "live.m3u8"
        const val INIT_NAME = "init.mp4"
        const val SEGMENT_PREFIX = "seg"
        const val SEGMENT_SUFFIX = ".m4s"

        /** Segments kept and listed: about six seconds at one-second fragments. */
        const val WINDOW = 6

        /** HLS players start three segments back from the live edge. */
        const val START_SEGMENTS = 3

        private const val BOX_HEADER = 8
        private const val MS_PER_SEC = 1000.0

        /** A box's size, reading the 64-bit `largesize` when the 32-bit field is 1; -1 if not all here. */
        private fun boxSize(
            bytes: ByteArray,
            at: Int,
        ): Long {
            val small = ByteBuffer.wrap(bytes, at, 4).int.toLong() and 0xFFFF_FFFFL
            if (small != 1L) return small
            if (bytes.size - at < BOX_HEADER + 8) return -1
            return ByteBuffer.wrap(bytes, at + BOX_HEADER, 8).long
        }
    }
}
