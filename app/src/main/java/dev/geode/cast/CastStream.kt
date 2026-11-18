package dev.geode.cast

import android.content.Context
import android.content.SharedPreferences
import android.net.ConnectivityManager
import android.net.LinkAddress
import android.net.Network
import android.net.NetworkCapabilities
import android.view.Surface
import dev.geode.RingLog
import dev.geode.audio.AudioBus
import dev.geode.data.LookSnapshot
import dev.geode.data.LookStore
import dev.geode.engine.bridge.CastNative
import dev.geode.engine.bridge.PerfNative
import dev.geode.engine.bridge.PlayoutNative
import dev.geode.engine.bridge.SoundNative
import dev.geode.export.EncoderSurface
import dev.geode.geodeContainer
import dev.geode.playback.PlaybackEngine
import dev.geode.util.bestEffort
import java.net.Inet4Address
import java.net.InetAddress
import java.util.concurrent.Executors
import java.util.concurrent.locks.LockSupport

/**
 * One cast: the phone's picture into the cast's encoders (core/cast/CastEncoder), served to the TV, a browser through "Show on a computer"
 * ([ComputerLink]), or both.
 *
 * The picture is the stage's own (owner: "make sure the app is not rendering twice 2 different things, but
 * merely sending what the phone does"). While the app's stage draws, its renderer copies each finished frame
 * into the cast's tee (core/viz/frame/CastTee: GPU buffers both contexts share, no read-back) and this thread
 * blits the newest into the encoder: one render, the exact style, preset, Auto step, preview, touch and tilt the
 * phone shows. The stage keeps drawing while the app is open on another tab ([CastMirror.streaming]).
 *
 * Only while the stage draws nothing (the app in the background, the screen off: casting keeps going then,
 * owner 2026-11-02) does a renderer of its own draw here, made on first need and freed after a stretch unused:
 * the saved live look, the stage's style and params as last seen ([CastMirror]), the MilkDrop preset it last
 * picked ([CastMirror.milkPath]) and the same sound ([AudioBus.heard]). It is a second engine, so its
 * Auto, director and MilkDrop randomness can drift from what the phone last showed; nobody sees the phone then.
 * The saved look is read off this thread and only the parts that changed are handed over, so a store write (a
 * track change, a slider) never stalls the encoder's clock or restarts modulation.
 *
 * The encoder, its clock, the PTS and the audio cut are this thread's for the whole stream whichever source
 * draws, so nothing restarts when the track, the look or the source changes.
 *
 * If the render loop dies after the stream started, [onFailed] runs (on the render thread): a
 * frozen picture on the TV with a muted phone is worse than ending the cast.
 *
 * A browser on "Show on a computer" gets the same picture over WebRTC when it can ([WebRtcLink]): the encoder's
 * own H.264 buffers, copied as they leave the codec (no second render, no second encode), and the same PCM as
 * Opus, run only while a browser is connected; its media is encrypted (DTLS-SRTP). Where WebRTC fails the page
 * plays the fMP4 feed as before. The TV keeps HLS. The encoders, their thread, the fragmented MP4 and the Opus
 * encoder's lifetime are native (core/cast/CastEncoder, Fmp4Writer); this thread draws and hands each frame's
 * sound over.
 *
 * The sound goes to the TV inside the stream (the phone is a silent remote, see [CastController]).
 * Pacing is native (core/viz/frame/FixedRateClock, #572): when a frame is due, its PTS and the audio cut; a stall
 * drops frames, never bursts them. Each frame queues 48 kHz audio up to that cut, from what the ring gained, padded
 * with silence while the music is paused, so the TV's player never waits for audio and picture and sound stay together.
 * That pump is native too (core/output/CastAudioPump, #1150).
 *
 * The encoders write into the server and WebRTC by their native handles, so those two are freed only after
 * the render thread closed the encoders, however late it ends ([freeLinksWhenDone]).
 */
internal class CastStream(
    context: Context,
    /** The player's ring handle, 0 for none: only its sound goes to the TV (core/output/CastAudioPump). */
    private val musicInput: Long,
    private val onFailed: () -> Unit,
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
    private var thread: Thread? = null

    @Volatile
    private var running = false

    /** Guards [rendering] and [stopped]: whichever of the render thread's end and [stop] comes last frees the links. */
    private val links = Any()
    private var rendering = false
    private var stopped = false

    /** The saved live look, read on [lookReader] whenever the app saves one; null until the first read lands. */
    @Volatile
    private var savedLook: LookSnapshot? = null
    private val lookReader =
        Executors.newSingleThreadExecutor { r -> Thread(r, "cast-look").apply { isDaemon = true } }
    private val lookListener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> readSavedLook() }

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
        prefsFiles.viz.registerOnSharedPreferenceChangeListener(lookListener)
        prefsFiles.general.registerOnSharedPreferenceChangeListener(lookListener)
        readSavedLook()
        running = true
        rendering = true
        thread =
            Thread({ runRenderLoop() }, "cast-render").apply {
                start()
            }
    }

    fun stop() {
        running = false
        bestEffort(TAG, "render.join()") { thread?.join(JOIN_MS) }
        thread = null
        prefsFiles.viz.unregisterOnSharedPreferenceChangeListener(lookListener)
        prefsFiles.general.unregisterOnSharedPreferenceChangeListener(lookListener)
        lookReader.shutdownNow()
        // Nobody is served from now on, even while a stuck render thread still holds the encoders.
        server.halt()
        freeLinksWhenDone(renderEnded = false)
    }

    /** Frees the server, then WebRTC, once the stream stopped and the render thread closed the encoders. */
    private fun freeLinksWhenDone(renderEnded: Boolean) {
        synchronized(links) {
            if (renderEnded) rendering = false else stopped = true
            if (rendering || !stopped) return
        }
        if (renderEnded) RingLog.note(TAG, "the render thread ended after stop")
        server.stop()
        webrtc.close()
    }

    /**
     * Reads the saved look on [lookReader], never on the render thread (it parses JSON and asks Settings for reduced
     * motion), keeping every part that did not change as the same object: [CastFallback] re-pushes a part
     * by identity, and each read used to make every part new, so any store write (a track change, a slider's save)
     * re-sent all the waves, envelopes and tuning and had native resolve the reactions again.
     */
    private fun readSavedLook() {
        bestEffort(TAG, "read the saved look") {
            lookReader.execute {
                val fresh = runCatching { LookStore(prefsFiles, appContext.contentResolver).current() }.getOrNull() ?: return@execute
                val old = savedLook
                savedLook =
                    if (old == null) {
                        fresh
                    } else {
                        fresh.copy(
                            params = if (fresh.params == old.params) old.params else fresh.params,
                            lfos = if (fresh.lfos == old.lfos) old.lfos else fresh.lfos,
                            adsrs = if (fresh.adsrs == old.adsrs) old.adsrs else fresh.adsrs,
                            reactions = if (fresh.reactions == old.reactions) old.reactions else fresh.reactions,
                            soundTuning = if (fresh.soundTuning == old.soundTuning) old.soundTuning else fresh.soundTuning,
                        )
                    }
            }
        }
    }

    private fun runRenderLoop() {
        runCatching { renderLoop() }.onFailure {
            RingLog.note(TAG, "render loop: $it")
            if (running) onFailed()
        }
    }

    /** The saved look and the mirrored look last handed to the fallback renderer; render thread only. */
    private var applied: LookSnapshot? = null
    private var mirrored: CastMirror.Look? = null

    /** The .milk last handed to the fallback renderer; render thread only. */
    private var mirroredMilk: String? = null

    /** Milliseconds since the last segment was cut, 0 before the first; [CastService]'s watchdog reads it. */
    val segmentAgeMs: Long get() = server.msSinceLastSegment

    private fun renderLoop() {
        val encoder = CastNative.castEncoderOpen(server.native, webrtc.native, musicInput)
        var surface: Surface? = null
        var egl: EncoderSurface? = null
        var fallback: CastFallback? = null
        var fallbackDrewNs = 0L
        var tee = 0L
        try {
            check(encoder != 0L) { "no cast encoder" }
            val input = CastNative.castEncoderSurface(encoder) as? Surface ?: error("no encoder surface")
            surface = input
            val out = EncoderSurface(input).also { egl = it }
            // The TV plays the ring as written, not the muted phone's sink: no reader waits for the speaker.
            PlayoutNative.playoutSetBypass(PlaybackEngine.playout, true)
            out.makeCurrent()
            // The stage's own picture wherever the stage draws; 0 on a driver without the GPU buffer extensions.
            tee = CastNative.castTeeOpen(WIDTH, HEIGHT, FPS)
            if (tee == 0L) RingLog.note(TAG, "no tee: the cast renders on its own")
            val teeOpenedNs = System.nanoTime()
            CastMirror.setStreaming(true)
            val clock = LongArray(PerfNative.FRAME_CLOCK_STATE)
            PerfNative.frameClockStart(clock, FPS, System.nanoTime())
            while (running) {
                check(!CastNative.castEncoderFailed(encoder)) { "the cast encoder failed" }
                LockSupport.parkNanos(PerfNative.frameClockWaitNs(clock, System.nanoTime()))
                val ptsNs = PerfNative.frameClockTake(clock, System.nanoTime())
                val now = System.nanoTime()
                val fromStage = tee != 0L && CastNative.castTeeDraw(tee, now, WIDTH, HEIGHT)
                val stageDraws = SoundNative.stageRun(CastMirror.stageFlags) and SoundNative.STAGE_DRAWS != 0
                if (!fromStage && tee != 0L && CastNative.castTeeAwaitStage(stageDraws, fallback != null, now - teeOpenedNs)) {
                    // The stage is on screen and its first copy is coming (a new surface stages its scene over
                    // several frames): wait for it rather than have a second renderer show another visual first.
                    CastNative.castEncoderPump(encoder, AudioBus.heard, PerfNative.frameClockAudioUntilUs(clock))
                    continue
                }
                if (!fromStage) {
                    val renderer = fallback ?: fallbackRenderer().also { fallback = it }
                    syncLook(renderer)
                    renderer.draw()
                    fallbackDrewNs = now
                } else if (fallback != null && CastNative.castFallbackRelease(now, fallbackDrewNs)) {
                    // The stage has led for a while: give the fallback's scenes and sound engine back.
                    bestEffort(TAG, "fallback.release()") { fallback.release() }
                    fallback = null
                }
                out.setPresentationTimeNs(ptsNs)
                out.swapBuffers()
                CastNative.castEncoderPump(encoder, AudioBus.heard, PerfNative.frameClockAudioUntilUs(clock))
            }
        } finally {
            CastMirror.setStreaming(false)
            PlayoutNative.playoutSetBypass(PlaybackEngine.playout, false)
            if (tee != 0L) bestEffort(TAG, "castTeeClose()") { CastNative.castTeeClose(tee) }
            bestEffort(TAG, "fallback.release()") { fallback?.release() }
            bestEffort(TAG, "egl.release()") { egl?.release() }
            bestEffort(TAG, "surface.release()") { surface?.release() }
            // After the surface is no longer drawn into; then the server and WebRTC may go.
            CastNative.castEncoderClose(encoder)
            freeLinksWhenDone(renderEnded = true)
        }
    }

    /** The renderer that draws while the stage does not; made on this thread, its context current. */
    private fun fallbackRenderer(): CastFallback {
        val renderer = CastFallback(appContext, WIDTH, HEIGHT)
        // A new renderer starts from the whole look and the stage's preset.
        applied = null
        mirrored = null
        mirroredMilk = null
        return renderer
    }

    /** The saved look when the app saves one, then the stage's style and params on top while [CastMirror] has them. */
    private fun syncLook(renderer: CastFallback) {
        val saved = savedLook
        if (saved != null && saved !== applied) {
            applied = saved
            // Only the parts that changed are new objects (readSavedLook), so only those are pushed again.
            renderer.look =
                saved.copy(
                    sceneId = mirrored?.sceneId ?: saved.sceneId,
                    params = mirrored?.params ?: saved.params,
                )
        }
        // The renderer restores the remembered preset only when it is built; every later pick is followed here.
        val milk = CastMirror.milkPath
        if (milk != null && milk != mirroredMilk) {
            mirroredMilk = milk
            renderer.loadMilkPreset(milk)
        }
        val look = CastMirror.look ?: return
        if (look === mirrored) return
        mirrored = look
        renderer.look = renderer.look.copy(sceneId = look.sceneId, params = look.params)
    }

    /** No Wi-Fi or Ethernet network with an IPv4 address: the TV has nothing to fetch the stream from. */
    class NoLanAddress : IllegalStateException("no Wi-Fi or Ethernet address to serve the TV from")

    companion object {
        const val TAG = "CastStream"

        // width, height, fps: core/viz/frame (CastFormat)
        private val castFormat = IntArray(CastNative.CAST_FORMAT_INTS).also { CastNative.castFormat(it) }
        private val WIDTH = castFormat[0]
        private val HEIGHT = castFormat[1]
        val FPS = castFormat[2]

        const val JOIN_MS = 2_000L

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
