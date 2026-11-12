package dev.geode.cast

import android.media.MediaCodec
import android.media.MediaFormat
import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import java.net.InetAddress
import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write

/**
 * "Show on a computer" over WebRTC, the shell's handle on core/cast/WebRtcLink (libdatachannel): the browser's
 * low-delay path, owner 2026-11-12 ("Use webrtc protocol. To have smooth play"). Every decision is native
 * (core/viz/frame/WebRtcRule): which of the browser's codecs to answer with, which candidates to keep (the
 * phone's own Wi-Fi or Ethernet address only, no STUN or TURN, nothing off the LAN), when a browser's picture
 * starts, the RTP timing. This only hands bytes over and keeps the handle alive while a call is in it.
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
    private var handle: Long = GeodeNative.webrtcOpen(address.hostAddress.orEmpty(), ipv4(address), prefix)

    /**
     * The native link, which the cast's server ([LiveLink]) asks for the answer to a browser's offer; 0 once
     * closed. The server keeps it, so it stops before this closes.
     */
    val native: Long get() = lock.read { handle }

    /** One H.264 buffer from the cast encoder ([CastEncoder]'s video tap): a frame, or the codec config. */
    fun video(
        bytes: ByteArray,
        size: Int,
        ptsUs: Long,
        key: Boolean,
        config: Boolean,
    ) = lock.read {
        if (handle == 0L) return@read
        if (config) GeodeNative.webrtcVideoConfig(handle, bytes, size) else GeodeNative.webrtcVideo(handle, bytes, size, ptsUs, key)
    }

    fun audio(
        bytes: ByteArray,
        size: Int,
        ptsUs: Long,
    ) = lock.read { if (handle != 0L) GeodeNative.webrtcAudio(handle, bytes, size, ptsUs) }

    /** Browsers connected now. */
    val peers: Int get() = lock.read { if (handle != 0L) GeodeNative.webrtcPeers(handle) else 0 }

    override fun close() =
        lock.write {
            if (handle != 0L) GeodeNative.webrtcClose(handle)
            handle = 0L
        }
}

/**
 * The sound for browsers on WebRTC: the cast's own PCM (only the player's music, as for the TV) encoded as Opus,
 * the codec every browser's WebRTC plays (it plays no AAC). Android's own Opus encoder (API 29+), 48 kHz stereo at
 * the cast's audio bit rate; [CastStream] runs one only while a browser is connected. Each packet goes to [link]
 * with its PTS, on the cast's sample clock like the AAC's.
 */
internal class OpusFeed(
    private val link: WebRtcLink,
) : AutoCloseable {
    private val codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_OPUS)
    private val input: CodecPcmFeed

    @Volatile
    private var running = true
    private val drain: Thread

    init {
        val format =
            MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_OPUS, CastEncoder.AUDIO_RATE_HZ, CastEncoder.CHANNELS).apply {
                setInteger(MediaFormat.KEY_BIT_RATE, CastEncoder.AUDIO_BITRATE)
            }
        codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        codec.start()
        input = CodecPcmFeed(codec, CastEncoder.AUDIO_RATE_HZ, CastEncoder.CHANNELS)
        drain =
            Thread({ drainLoop() }, "cast-opus").apply {
                isDaemon = true
                start()
            }
    }

    /** The same block the cast's AAC just took. Render thread. */
    fun queue(
        pcm: ShortArray,
        frames: Int,
    ) = input.queue(pcm, frames)

    private fun drainLoop() {
        val info = MediaCodec.BufferInfo()
        runCatching {
            while (running) {
                val index = codec.dequeueOutputBuffer(info, DRAIN_WAIT_US)
                if (index < 0) continue
                val buffer = codec.getOutputBuffer(index)
                // The first buffer is the codec config (the Opus header): WebRTC carries no such thing.
                if (buffer != null && info.size > 0 && info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG == 0) {
                    val bytes = ByteArray(info.size)
                    buffer.position(info.offset)
                    buffer.get(bytes, 0, info.size)
                    link.audio(bytes, info.size, info.presentationTimeUs)
                }
                codec.releaseOutputBuffer(index, false)
            }
        }.onFailure { RingLog.note(TAG, "drain: ${it.javaClass.simpleName}") }
    }

    override fun close() {
        if (!running) return
        running = false
        bestEffort(TAG, "drain.join()") { drain.join(JOIN_MS) }
        bestEffort(TAG, "codec.stop()") { codec.stop() }
        bestEffort(TAG, "codec.release()") { codec.release() }
    }

    private companion object {
        const val TAG = "CastOpus"
        const val DRAIN_WAIT_US = 10_000L
        const val JOIN_MS = 1_000L
    }
}
