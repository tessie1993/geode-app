package dev.geode.cast

import dev.geode.engine.bridge.WebrtcNative
import java.net.InetAddress
import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write

/**
 * "Show on a computer" over WebRTC, the shell's handle on core/cast/WebRtcLink (libdatachannel): the browser's
 * low-delay path, owner 2026-11-12 ("Use webrtc protocol. To have smooth play"). Every decision is native
 * (core/viz/frame/WebRtcRule): which of the browser's codecs to answer with, which candidates to keep (the
 * phone's own Wi-Fi or Ethernet address only, no STUN or TURN, nothing off the LAN), when a browser's picture
 * starts, the RTP timing. The cast's native encoders send it the H.264 and Opus (core/cast/CastEncoder); this
 * keeps the handle alive while a call is in it.
 *
 * What is encrypted: the picture and sound sent over WebRTC (DTLS-SRTP). The page, the link and the offer and
 * answer that set the call up go over the same plain-http server as before, so a listener on the Wi-Fi cannot
 * read the WebRTC media, but anyone who has the link can still open it, and someone who can alter the Wi-Fi's
 * traffic could put themselves in between.
 *
 * One per [CastStream], bound to the address its server ([LiveLink]) listens on. [close] waits for a call in flight.
 */
internal class WebRtcLink(
    address: InetAddress,
    prefix: Int,
) : AutoCloseable {
    private val lock = ReentrantReadWriteLock()
    private var handle: Long = WebrtcNative.webrtcOpen(address.hostAddress.orEmpty(), ipv4(address), prefix)

    /**
     * The native link, which the cast's server ([LiveLink]) asks for the answer to a browser's offer and the
     * encoders write to; 0 once closed. Both keep it, so they stop before this closes.
     */
    val native: Long get() = lock.read { handle }

    override fun close() =
        lock.write {
            if (handle != 0L) WebrtcNative.webrtcClose(handle)
            handle = 0L
        }
}
