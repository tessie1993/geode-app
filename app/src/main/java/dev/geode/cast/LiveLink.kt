package dev.geode.cast

import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import java.io.IOException
import java.net.Inet4Address
import java.net.InetAddress
import java.nio.ByteBuffer
import java.nio.channels.WritableByteChannel
import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write

/**
 * The shell's handle on the cast's HTTP server (core/cast/LiveServer): the live stream served over the local
 * network, to the TV's Cast receiver and to a browser through "Show on a computer". With the Cast SDK's own
 * traffic and [WebRtcLink], this is the one thing the app's INTERNET permission is for (the owner's rule: Cast is
 * the only internet use).
 *
 * Everything is native: the listening socket on the phone's own Wi-Fi or Ethernet address (never the wildcard),
 * who is let in, the request parser and its bounds, the segment window, the playlist and every response. This
 * hands the muxer's bytes over ([sink]: Media3's muxer writes to a Java channel), says who the stream is for
 * ([admit]) and reads back the few values the shell shows or watches.
 *
 * One per [CastStream]. [stop] must run before the stream's [WebRtcLink] closes: the server asks that link for
 * the answer to a browser's offer.
 */
internal class LiveLink(
    address: InetAddress,
    prefix: Int,
    webrtc: WebRtcLink,
) {
    private val lock = ReentrantReadWriteLock()
    private var handle: Long =
        GeodeNative.castServerOpen(address.hostAddress.orEmpty(), ipv4(address), prefix, webrtc.native)

    /** Where the muxer writes its fragmented MP4, in whatever pieces it likes. Closing it ends nothing here. */
    val sink: WritableByteChannel =
        object : WritableByteChannel {
            @Volatile
            private var open = true

            override fun isOpen(): Boolean = open

            override fun close() {
                open = false
            }

            override fun write(src: ByteBuffer): Int {
                val count = src.remaining()
                val chunk = ByteArray(count)
                src.get(chunk)
                lock.read { if (handle != 0L) GeodeNative.castServerWrite(handle, chunk, count) }
                return count
            }
        }

    /** Binds and starts serving; throws when the address cannot be bound. */
    fun start() {
        val port = lock.read { if (handle != 0L) GeodeNative.castServerStart(handle) else 0 }
        if (port == 0) throw IOException("the cast server could not bind")
    }

    /** Lets in the Cast device at [tv] while [forTv], and the bound subnet while [forLink]; nobody else. */
    fun admit(
        tv: InetAddress?,
        forTv: Boolean,
        forLink: Boolean,
    ) = lock.read {
        val wants = (if (forTv) GeodeNative.LINK_WANT_TV else 0) or (if (forLink) GeodeNative.LINK_WANT_LINK else 0)
        if (handle != 0L) GeodeNative.castServerAdmit(handle, ipv4(tv), wants)
    }

    /** The playlist's URL, as the receiver loads it; valid after [start]. */
    val playlistUrl: String get() = text(GeodeNative.CAST_TEXT_PLAYLIST_URL)

    /** "Show on a computer"'s link as the user sees, copies and shares it (the host as one number, not dotted). */
    val shareUrl: String get() = text(GeodeNative.CAST_TEXT_SHARE_URL)

    /** A short name for this link ("Blue Comet") to show beside its QR code. */
    val label: String get() = text(GeodeNative.CAST_TEXT_LABEL)

    /** When the TV last got a file (SystemClock.elapsedRealtime, 0 = never): [CastService] asks if it still plays. */
    val lastReceiverFetchMs: Long get() = state()[GeodeNative.CAST_STATE_LAST_FETCH_MS]

    /** True once the playlist lists enough segments for the receiver to start on. */
    val isReady: Boolean get() = state()[GeodeNative.CAST_STATE_READY] != 0L

    /** Milliseconds since the last segment was cut; 0 before the first. */
    val msSinceLastSegment: Long get() = state()[GeodeNative.CAST_STATE_SEGMENT_AGE_MS]

    /**
     * Ends every connection and frees the server. What it refused and what it could not find are noted once,
     * here, in [RingLog]: counts and the first refused address, never a path or the token.
     */
    fun stop() =
        lock.write {
            if (handle == 0L) return@write
            GeodeNative.castServerStop(handle)
            val refusals = GeodeNative.castServerText(handle, GeodeNative.CAST_TEXT_REFUSALS).orEmpty()
            if (refusals.isNotEmpty()) RingLog.note(TAG, refusals)
            val state = LongArray(GeodeNative.CAST_STATE)
            GeodeNative.castServerState(handle, state)
            val asMuxed = state[GeodeNative.CAST_STATE_AS_MUXED]
            if (asMuxed > 0L) RingLog.note(TAG, "$asMuxed fragments served as muxed")
            if (state[GeodeNative.CAST_STATE_BROKEN] != 0L) RingLog.note(TAG, "the muxer's output could not be cut")
            GeodeNative.castServerClose(handle)
            handle = 0L
        }

    private fun text(which: Int): String = lock.read { if (handle != 0L) GeodeNative.castServerText(handle, which).orEmpty() else "" }

    private fun state(): LongArray =
        lock.read {
            val out = LongArray(GeodeNative.CAST_STATE)
            if (handle != 0L) GeodeNative.castServerState(handle, out)
            out
        }

    private companion object {
        const val TAG = "CastHttp"
    }
}

/** [address] as IPv4 in host order, the form the native rules read; 0 when it is null or not IPv4. */
internal fun ipv4(address: InetAddress?): Int = if (address is Inet4Address) ByteBuffer.wrap(address.address).int else 0
