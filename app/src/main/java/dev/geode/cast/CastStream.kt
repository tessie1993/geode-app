package dev.geode.cast

import android.content.Context
import android.content.SharedPreferences
import android.net.ConnectivityManager
import android.net.LinkAddress
import android.net.Network
import android.net.NetworkCapabilities
import dev.geode.RingLog
import dev.geode.data.LookSnapshot
import dev.geode.data.LookStore
import dev.geode.engine.bridge.CastNative
import dev.geode.engine.bridge.PlayoutNative
import dev.geode.geodeContainer
import dev.geode.model.SceneParams
import dev.geode.playback.PlaybackEngine
import dev.geode.util.backgroundThreadFactory
import dev.geode.util.bestEffort
import java.net.Inet4Address
import java.net.InetAddress
import java.util.concurrent.Executors

/**
 * One cast: the phone's picture into the cast's encoders (core/cast/CastEncoder), served to the TV, a browser through "Show on a computer"
 * ([ComputerLink]), or both.
 *
 * The picture is the stage's own (owner: "make sure the app is not rendering twice 2 different things, but
 * merely sending what the phone does"). While the app's stage draws, its renderer copies each finished frame
 * into the cast's tee (core/viz/frame/CastTee: GPU buffers both contexts share, no read-back) and the render thread
 * blits the newest into the encoder: one render, the exact style, preset, Auto step, preview, touch and tilt the
 * phone shows. The stage keeps drawing while the app is open on another tab ([CastMirror.streaming]).
 *
 * Only while the stage draws nothing (the app in the background, the screen off: casting keeps going then,
 * owner 2026-11-02) does a renderer of its own draw there, made on first need and freed after a stretch unused:
 * the saved live look, the stage's style and params as last seen ([CastMirror]), and the same sound.
 * It is a second engine, so its Auto and director randomness can drift from what the phone last showed;
 * nobody sees the phone then.
 * The saved look is read off the main thread and handed over whole when it or the mirrored look changes; native
 * passes on only the parts that differ, so a store write (a track change, a slider) never stalls the encoder's
 * clock or restarts modulation.
 *
 * The render thread is native (core/output/android/CastRenderHost): this class starts it, hands it the look, polls it
 * and stops it. The encoder, its clock, the PTS and the audio cut are its for the whole stream whichever source
 * draws, so nothing restarts when the track, the look or the source changes.
 *
 * If the render thread dies after the stream started, [renderFailed] says so and [CastService]'s watchdog ends the
 * cast: a frozen picture on the TV with a muted phone is worse than ending the cast.
 *
 * A browser on "Show on a computer" gets the same picture over WebRTC when it can ([WebRtcLink]): the encoder's
 * own H.264 buffers, copied as they leave the codec (no second render, no second encode), and the same PCM as
 * Opus, run only while a browser is connected; its media is encrypted (DTLS-SRTP). Where WebRTC fails the page
 * plays the fMP4 feed as before. The TV keeps HLS. The encoders, their thread, the fragmented MP4 and the Opus
 * encoder's lifetime are native (core/cast/CastEncoder, Fmp4Writer).
 *
 * The sound goes to the TV inside the stream (the phone is a silent remote, see [CastController]).
 * Pacing is native (core/viz/frame/FixedRateClock, #572): when a frame is due, its PTS and the audio cut; a stall
 * drops frames, never bursts them. Each frame queues 48 kHz audio up to that cut, from what the ring gained, padded
 * with silence while the music is paused, so the TV's player never waits for audio and picture and sound stay together.
 * That pump is native too (core/output/CastAudioPump, #1150).
 *
 * The encoders write into the server and WebRTC by their native handles, so those two are freed only after
 * the render thread closed the encoders, however late it ends ([stop]).
 */
internal class CastStream(
    context: Context,
    /** The player's ring handle, 0 for none: only its sound goes to the TV (core/output/CastAudioPump). */
    private val musicInput: Long,
) {
    private val appContext = context.applicationContext
    private val prefsFiles = appContext.geodeContainer.prefsFiles
    private val link: LinkAddress = lanLinkAddress(appContext) ?: throw NoLanAddress()

    /** The Wi-Fi or Ethernet address the stream is served from. */
    val address: InetAddress = link.address

    /** WebRTC for browsers, bound to the same address; closed after the server and the encoder stop. */
    private val webrtc = WebRtcLink(address, link.prefixLength)
    private val server = LiveLink(address, link.prefixLength, webrtc)

    /** Who may connect: the Cast device at [tv] while [forTv], the Wi-Fi's subnet while [forLink] (native rule). */
    fun admit(
        tv: InetAddress?,
        forTv: Boolean,
        forLink: Boolean,
    ) = server.admit(tv, forTv, forLink)

    /** When the TV last fetched a file (SystemClock.elapsedRealtime, 0 = never). */
    val lastReceiverFetchMs: Long get() = server.lastReceiverFetchMs

    /** The native render thread's handle, 0 before [start] and once [stop] took it; guarded by [renderLock]. */
    private var render = 0L
    private val renderLock = Any()
    private val paramFrame = FloatArray(SceneParams.FIELDS)

    /** The saved live look, read on [lookReader] whenever the app saves one; null until the first read lands. */
    @Volatile
    private var savedLook: LookSnapshot? = null
    private val lookReader =
        Executors.newSingleThreadExecutor(backgroundThreadFactory("cast-look"))
    private val lookListener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> readSavedLook() }
    private val mirrorListener = { pushLook() }

    /** The URL the receiver loads; valid after [start]. */
    val playlistUrl: String get() = server.playlistUrl

    /** The page a browser opens ("Show on a computer"), as the user is shown it (no dotted address); valid after [start]. */
    val shareUrl: String get() = server.shareUrl

    /** The short link a computer types (no token; the page asks for the code the sheet shows); valid after [start]. */
    val pairUrl: String get() = server.pairUrl

    /** The link's short name, shown beside its QR code. */
    val shareLabel: String get() = server.label

    /** True once the playlist lists enough segments for the receiver to start on. */
    val isReady: Boolean get() = server.isReady

    fun start() {
        try {
            server.start()
        } catch (e: java.io.IOException) {
            server.stop()
            webrtc.close()
            throw e
        }
        val handle =
            CastNative.castRenderStart(
                server.native,
                webrtc.native,
                musicInput,
                appContext.assets,
                appContext.cacheDir.absolutePath,
            )
        if (handle == 0L) {
            server.stop()
            webrtc.close()
            error("no cast render thread")
        }
        synchronized(renderLock) { render = handle }
        // The TV plays the ring as written, not the muted phone's sink: no reader waits for the speaker.
        flags.take(this) {
            PlayoutNative.playoutSetBypass(PlaybackEngine.playout, true)
            CastMirror.setStreaming(true)
        }
        prefsFiles.viz.registerOnSharedPreferenceChangeListener(lookListener)
        prefsFiles.general.registerOnSharedPreferenceChangeListener(lookListener)
        CastMirror.onLook = mirrorListener
        pushLook()
        readSavedLook()
    }

    /** Ends the render thread, then frees the server and WebRTC it wrote into; called off the main thread. */
    fun stop() {
        CastMirror.onLook = null
        prefsFiles.viz.unregisterOnSharedPreferenceChangeListener(lookListener)
        prefsFiles.general.unregisterOnSharedPreferenceChangeListener(lookListener)
        lookReader.shutdownNow()
        val handle = synchronized(renderLock) { render.also { render = 0L } }
        if (handle != 0L && !CastNative.castRenderStop(handle, JOIN_MS)) RingLog.note(TAG, "the render thread is slow to stop")
        flags.release(this) {
            CastMirror.setStreaming(false)
            PlayoutNative.playoutSetBypass(PlaybackEngine.playout, false)
        }
        // Nobody is served from now on, even while a stuck render thread still holds the encoders.
        server.halt()
        // Joins, however late the thread ends: only then do the encoders stop writing into the server and WebRTC.
        if (handle != 0L) CastNative.castRenderFree(handle)
        server.stop()
        webrtc.close()
    }

    /** True once the render thread ended on an error it was not asked to: [CastService]'s watchdog ends the cast. */
    val renderFailed: Boolean get() = synchronized(renderLock) { render != 0L && CastNative.castRenderFailed(render) }

    /** The fallback renderer's Auto director moments since the last call; Auto steps on them while the stage draws nothing. */
    fun takeDirectorMoments(): Int = synchronized(renderLock) { if (render == 0L) 0 else CastNative.castRenderTakeDirectorMoments(render) }

    /**
     * Reads the saved look on [lookReader], never on the main thread (it parses JSON and asks Settings for reduced
     * motion), then hands it on.
     */
    private fun readSavedLook() {
        bestEffort(TAG, "read the saved look") {
            lookReader.execute {
                savedLook = runCatching { LookStore(prefsFiles, appContext.contentResolver).current() }.getOrNull() ?: return@execute
                pushLook()
            }
        }
    }

    /** The saved look when the app saves one, then the stage's style and params on top while [CastMirror] has them. */
    private fun pushLook() {
        val saved = savedLook ?: LookSnapshot()
        val mirrored = CastMirror.look
        val look = if (mirrored == null) saved else saved.copy(sceneId = mirrored.sceneId, params = mirrored.params)
        synchronized(renderLock) {
            if (render == 0L) return
            look.params.frameInto(paramFrame)
            CastNative.castRenderSetLook(
                render,
                look.sceneId,
                paramFrame,
                look.lfos,
                look.adsrs,
                look.soundTuning?.toFloatArray(),
                look.reactions,
                look.reducedMotion,
                look.transitionId,
                look.transitionMs,
            )
        }
    }

    /** Milliseconds since the last segment was cut, 0 before the first; [CastService]'s watchdog reads it. */
    val segmentAgeMs: Long get() = server.msSinceLastSegment

    /** No Wi-Fi or Ethernet network with an IPv4 address: the TV has nothing to fetch the stream from. */
    class NoLanAddress : IllegalStateException("no Wi-Fi or Ethernet address to serve the TV from")

    companion object {
        const val TAG = "CastStream"

        const val JOIN_MS = 2_000L

        /** The stream holding the playout bypass and [CastMirror.streaming], both plain flags for the whole process. */
        private val flags = CastFlagHolder()

        /**
         * This phone's IPv4 address on the Wi-Fi or Ethernet the TV shares, or null. Not simply the active
         * network: with a VPN up, or cellular preferred, that one has no address the TV can reach.
         */
        fun lanAddress(context: Context): InetAddress? = lanLinkAddress(context)?.address

        /** [lanAddress] with its prefix length: the subnet "Show on a computer" admits. */
        @Suppress("DEPRECATION") // allNetworks, as in lanAddresses.
        fun lanLinkAddress(context: Context): LinkAddress? {
            val connectivity = context.getSystemService(ConnectivityManager::class.java) ?: return null
            val candidates = (listOfNotNull(connectivity.activeNetwork) + connectivity.allNetworks).distinct()
            return candidates.firstNotNullOfOrNull { lanAddressOf(connectivity, it) }
        }

        /**
         * Every such address, the active network's first. The stream stops when the one it serves from is no
         * longer here (the phone left that Wi-Fi, or it changed address): the link would name nothing.
         */
        @Suppress("DEPRECATION") // allNetworks: the one synchronous list; a callback would make start async.
        fun lanAddresses(context: Context): List<InetAddress> {
            val connectivity = context.getSystemService(ConnectivityManager::class.java) ?: return emptyList()
            val candidates = (listOfNotNull(connectivity.activeNetwork) + connectivity.allNetworks).distinct()
            return candidates.mapNotNull { lanAddressOf(connectivity, it)?.address }
        }

        private fun lanAddressOf(
            connectivity: ConnectivityManager,
            network: Network,
        ): LinkAddress? {
            val caps = connectivity.getNetworkCapabilities(network) ?: return null
            if (caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) return null
            val local = caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) || caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)
            if (!local) return null
            return connectivity
                .getLinkProperties(network)
                ?.linkAddresses
                ?.firstOrNull { it.address is Inet4Address && it.address.isSiteLocalAddress }
        }
    }
}

/**
 * Which stream holds process-wide flags its render thread sets. A stopped stream's render thread can end after
 * the next stream set them (it is stopped on a thread of its own and may outlive the join), so it clears them only
 * while it is still their holder.
 */
internal class CastFlagHolder {
    private var holder: Any? = null

    @Synchronized
    fun take(
        stream: Any,
        set: () -> Unit,
    ) {
        holder = stream
        set()
    }

    @Synchronized
    fun release(
        stream: Any,
        clear: () -> Unit,
    ) {
        if (holder !== stream) return
        holder = null
        clear()
    }
}
