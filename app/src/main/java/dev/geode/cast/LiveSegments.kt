package dev.geode.cast

import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.channels.WritableByteChannel

/**
 * The live HLS stream the TV plays, built from the fragmented MP4 that `FragmentedMp4Muxer` writes.
 *
 * The muxer writes one continuous file: `ftyp` and `moov` once, then a `moof` and `mdat` pair per
 * fragment. HLS with fMP4 segments (CMAF) wants exactly that split up: the header as the init
 * segment (`#EXT-X-MAP`) and each `moof`+`mdat` as one media segment. So this is the channel the
 * muxer writes to, and it cuts the bytes at box boundaries as they arrive. The muxer's fragments only
 * make sense inside its one file (absolute offsets, no decode time), so each is rewritten natively
 * into a self-contained segment before it is kept (`GeodeNative.hlsRewriteFragment`).
 *
 * Only the newest [WINDOW] segments are kept, in memory: a live playlist only ever names the last
 * few, and the receiver starts near the live edge. The timing is native (core/viz/frame/HlsSegments,
 * #656): each segment's `#EXTINF` is the media time its fragment holds, read from its sample
 * durations, never the wall time between two cuts (which follows how fast the encoder drained), and
 * the playlist text, its `#EXT-X-TARGETDURATION` and when it is first served come from there too.
 * This class only cuts, keeps and hands out bytes.
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

    /** Where the next box starts in the muxer's output, and where the pending fragment's moof did. */
    private var fileAt = 0L
    private var moofAt = 0L
    private val decodeTimes = LongArray(GeodeNative.HLS_TRACKS)

    /** Wall time of the last cut, for [msSinceLastSegment] (the freeze watchdog) only, never for timing. */
    private var lastCutMs = 0L
    private var open = true

    @Volatile
    private var init: ByteArray? = null

    @Volatile
    private var master: String? = null

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
            fileAt += size
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
            "moof" -> {
                moofAt = fileAt
                fragment = ByteArrayOutputStream().apply { write(bytes, offset, size) }
            }
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

    private fun publish(raw: ByteArray) {
        lastCutMs = nowMs()
        val bytes = init?.let { GeodeNative.hlsRewriteFragment(it, moofAt, raw, decodeTimes) } ?: raw
        if (bytes === raw) RingLog.note(TAG, "fragment $nextSequence served as muxed")
        val duration = init?.let { GeodeNative.hlsFragmentSeconds(it, bytes) } ?: 0.0
        segments.addLast(Segment(nextSequence++, bytes, duration))
        while (segments.size > WINDOW) segments.removeFirst()
    }

    /** Milliseconds since the last segment was cut (or the header was written); 0 before that. */
    @Synchronized
    fun msSinceLastSegment(): Long = if (lastCutMs == 0L) 0L else nowMs() - lastCutMs

    /** The init segment, or null until the muxer has written its header. */
    fun initSegment(): ByteArray? = init

    /** A browser's master playlist (native: CODECS from [init]'s avcC and esds), or null until the header is here. */
    fun masterPlaylist(): String? {
        master?.let { return it }
        val header = init ?: return null
        return GeodeNative.linkMasterPlaylist(header)?.also { master = it }
    }

    @Synchronized
    fun segment(sequence: Long): ByteArray? = segments.firstOrNull { it.sequence == sequence }?.bytes

    /**
     * The browser feed's next fragment after sequence [after] (its sequence and bytes), or null while there is none
     * yet. A new feed ([after] -1) starts at the newest, the live edge; a feed that fell out of the window skips to
     * the newest too, since a browser holding half a second would rather jump than play old seconds.
     */
    @Synchronized
    fun nextAfter(after: Long): Pair<Long, ByteArray>? {
        val newest = segments.lastOrNull() ?: return null
        if (newest.sequence <= after) return null
        val next = if (after < 0) newest else segments.firstOrNull { it.sequence == after + 1 } ?: newest
        return next.sequence to next.bytes
    }

    /** The live media playlist, or null until there is enough for the receiver to start on (native). */
    @Synchronized
    fun playlist(): String? {
        if (init == null || segments.isEmpty()) return null
        val durations = DoubleArray(segments.size) { segments[it].durationSec }
        return GeodeNative.hlsPlaylist(segments.first().sequence, durations, INIT_NAME, SEGMENT_PREFIX, SEGMENT_SUFFIX)
    }

    companion object {
        // The names cast/LiveHttpServer's routes answer (core/viz/frame/LinkShare: kPlaylistName and the rest).
        const val PLAYLIST_NAME = "live.m3u8"
        const val INIT_NAME = "init.mp4"
        const val SEGMENT_PREFIX = "seg"
        const val SEGMENT_SUFFIX = ".m4s"

        /** Segments kept and listed: about six seconds at one-second fragments (native, GEODE_HLS_WINDOW). */
        val WINDOW = GeodeNative.hlsWindow()

        private const val BOX_HEADER = 8
        private const val TAG = "LiveSegments"

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
