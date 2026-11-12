package dev.geode.cast

import android.content.Context
import android.content.SharedPreferences
import android.net.ConnectivityManager
import android.net.LinkAddress
import android.net.Network
import android.net.NetworkCapabilities
import dev.geode.RingLog
import dev.geode.audio.AudioBus
import dev.geode.data.LookStore
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audio.SoundCursor
import dev.geode.engine.bridge.GeodeNative
import dev.geode.export.EncoderSurface
import dev.geode.export.PcmResampler
import dev.geode.geodeContainer
import dev.geode.playback.PlaybackEngine
import dev.geode.render.PacedRate
import dev.geode.render.VisualizerRenderer
import dev.geode.render.look.LookSnapshot
import dev.geode.util.bestEffort
import java.net.Inet4Address
import java.net.InetAddress
import java.util.concurrent.Executors
import java.util.concurrent.locks.LockSupport

/**
 * One cast: the phone's picture into [CastEncoder], served to the TV, a browser through "Show on a computer"
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
 * picked ([CastMirror.milkPath]) and the same sound ([AudioBus.soundInput]). It is a second engine, so its
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
 * The sound goes to the TV inside the stream (the phone is a silent remote, see [CastController]).
 * Pacing is native (core/viz/frame/FixedRateClock, #572): when a frame is due, its PTS and the audio cut; a stall
 * drops frames, never bursts them. Each frame queues 48 kHz audio up to that cut, from what the ring gained, padded
 * with silence while the music is paused, so the TV's player never waits for audio and picture and sound stay together.
 * Those counts are native too (core/viz/frame/CastAudio, #772).
 */
internal class CastStream(
    context: Context,
    /** The player's own feed: only its sound goes to the TV (see [AudioPump]). */
    private val musicInput: SampleRing?,
    private val onFailed: () -> Unit,
) {
    private val appContext = context.applicationContext
    private val prefsFiles = appContext.geodeContainer.prefsFiles
    private val segments = LiveSegments()

    private val link: LinkAddress = lanLinkAddress(appContext) ?: throw NoLanAddress()

    /** The Wi-Fi or Ethernet address the stream is served from. */
    val address: InetAddress = link.address
    private val server = LiveHttpServer(segments, address, link.prefixLength)

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

    /** The saved live look, read on [lookReader] whenever the app saves one; null until the first read lands. */
    @Volatile
    private var savedLook: LookSnapshot? = null
    private val lookReader =
        Executors.newSingleThreadExecutor { r -> Thread(r, "cast-look").apply { isDaemon = true } }
    private val lookListener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> readSavedLook() }

    /** The URL the receiver loads; valid after [start]. */
    val playlistUrl: String get() = server.playlistUrl

    /** The page a browser opens ("Show on a computer"); valid after [start]. */
    val pageUrl: String get() = server.pageUrl

    /** True once the playlist lists enough segments for the receiver to start on. */
    val isReady: Boolean get() = segments.playlist() != null

    fun start() {
        try {
            server.start()
        } catch (e: java.io.IOException) {
            server.stop()
            throw e
        }
        prefsFiles.viz.registerOnSharedPreferenceChangeListener(lookListener)
        prefsFiles.general.registerOnSharedPreferenceChangeListener(lookListener)
        readSavedLook()
        running = true
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
        server.stop()
    }

    /**
     * Reads the saved look on [lookReader], never on the render thread (it parses JSON and asks Settings for reduced
     * motion), keeping every part that did not change as the same object: [VisualizerRenderer] re-pushes a part
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

    /** True once segments stopped arriving for [FROZEN_MS]: the encoder or the renderer stalled. */
    val isFrozen: Boolean get() = segments.msSinceLastSegment() > FROZEN_MS

    private fun renderLoop() {
        val encoder = CastEncoder(segments)
        val egl = EncoderSurface(encoder.surface)
        val audio = AudioPump(encoder, musicInput)
        var fallback: VisualizerRenderer? = null
        var fallbackDrewNs = 0L
        var tee = 0L
        try {
            // The TV plays the ring as written, not the muted phone's sink: no reader waits for the speaker.
            GeodeNative.playoutSetBypass(PlaybackEngine.playout, true)
            egl.makeCurrent()
            // The stage's own picture wherever the stage draws; 0 on a driver without the GPU buffer extensions.
            tee = GeodeNative.castTeeOpen(encoder.width, encoder.height, CastEncoder.FPS)
            if (tee == 0L) RingLog.note(TAG, "no tee: the cast renders on its own")
            val teeOpenedNs = System.nanoTime()
            CastMirror.setStreaming(true)
            val clock = LongArray(GeodeNative.FRAME_CLOCK_STATE)
            GeodeNative.frameClockStart(clock, CastEncoder.FPS, System.nanoTime())
            while (running) {
                LockSupport.parkNanos(GeodeNative.frameClockWaitNs(clock, System.nanoTime()))
                val ptsNs = GeodeNative.frameClockTake(clock, System.nanoTime())
                val now = System.nanoTime()
                val fromStage = tee != 0L && GeodeNative.castTeeDraw(tee, now, encoder.width, encoder.height)
                if (!fromStage && tee != 0L && fallback == null && now - teeOpenedNs < STAGE_FIRST_COPY_NS) {
                    // The stage's first copy is a frame away: wait for it rather than build a second renderer.
                    audio.pump(untilUs = GeodeNative.frameClockAudioUntilUs(clock))
                    continue
                }
                if (!fromStage) {
                    val renderer = fallback ?: fallbackRenderer(encoder).also { fallback = it }
                    syncLook(renderer)
                    renderer.onDrawFrame(null)
                    fallbackDrewNs = now
                } else if (fallback != null && now - fallbackDrewNs > FALLBACK_IDLE_NS) {
                    // The stage has led for a while: give the fallback's scenes and sound engine back.
                    bestEffort(TAG, "fallback.releaseScenes()") { fallback?.releaseScenes() }
                    fallback = null
                }
                egl.setPresentationTimeNs(ptsNs)
                egl.swapBuffers()
                audio.pump(untilUs = GeodeNative.frameClockAudioUntilUs(clock))
            }
        } finally {
            CastMirror.setStreaming(false)
            GeodeNative.playoutSetBypass(PlaybackEngine.playout, false)
            if (tee != 0L) bestEffort(TAG, "castTeeClose()") { GeodeNative.castTeeClose(tee) }
            bestEffort(TAG, "fallback.releaseScenes()") { fallback?.releaseScenes() }
            audio.close()
            egl.release()
            encoder.release()
        }
    }

    /** The renderer that draws while the stage does not; made on this thread, its context current. */
    private fun fallbackRenderer(encoder: CastEncoder): VisualizerRenderer {
        val renderer = VisualizerRenderer(appContext)
        renderer.soundInput = { AudioBus.soundInput }
        // The encoder's fixed rate, not the stage's cap: at 30 this renderer is keeping up, so its fluid
        // monitor and the shared thermal trend must not read its frames against the phone's 60 (or 120).
        val castFps = CastEncoder.FPS.toFloat()
        renderer.pacedRate = PacedRate { castFps }
        renderer.onDirectorStyleMoment = CastMirror::directorMoment
        renderer.onSurfaceCreated(null, null)
        renderer.onSurfaceChanged(null, encoder.width, encoder.height)
        // A new renderer starts from the whole look and the stage's preset.
        applied = null
        mirrored = null
        mirroredMilk = null
        return renderer
    }

    /** The saved look when the app saves one, then the stage's style and params on top while [CastMirror] has them. */
    private fun syncLook(renderer: VisualizerRenderer) {
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

    /**
     * Queues 48 kHz stereo up to each frame's audio cut. The counts are native
     * (core/viz/frame/CastAudio, #772): how much of what the ring gained (resampled here) to hold,
     * how much to queue from the hold, how much silence to pad while the music is paused, and the
     * running sample position. This keeps the PCM and calls the resampler and the encoder.
     *
     * Only the player's music is sent ([musicInput]). While the microphone or another app's audio
     * owns the ring, the visuals still hear it but the TV gets silence: the room's sound must not
     * leave the phone, and the mic would hear the TV and feed back.
     */
    private class AudioPump(
        private val encoder: CastEncoder,
        private val musicInput: SampleRing?,
    ) : AutoCloseable {
        private var input: SampleRing? = null
        private var cursor: SoundCursor? = null
        private var resampler: PcmResampler? = null
        private var resamplerInRate = 0
        private val state = LongArray(GeodeNative.CAST_AUDIO_STATE)
        private val step = LongArray(GeodeNative.CAST_AUDIO_STEP)
        private val hold = ShortArray(GeodeNative.castAudioStart(state, CastEncoder.AUDIO_RATE_HZ) * CH)
        private var block = ShortArray(0)

        fun pump(untilUs: Long) {
            val gained = takeFromRing()
            val held = state[HELD].toInt()
            GeodeNative.castAudioStep(state, (gained?.size ?: 0) / CH, untilUs, step)
            val evict = step[EVICT].toInt()
            if (evict > 0) System.arraycopy(hold, evict * CH, hold, 0, (held - evict) * CH)
            if (gained != null) System.arraycopy(gained, step[SKIP].toInt() * CH, hold, (held - evict) * CH, step[KEEP].toInt() * CH)
            val take = step[TAKE].toInt()
            val need = take + step[PAD].toInt()
            if (need <= 0) return
            if (block.size < need * CH) block = ShortArray(need * CH)
            System.arraycopy(hold, 0, block, 0, take * CH)
            block.fill(SILENCE, take * CH, need * CH)
            System.arraycopy(hold, take * CH, hold, 0, state[HELD].toInt() * CH)
            encoder.queueAudio(block, need)
        }

        /** What the ring gained since the last call, resampled to the stream's rate; null for none. */
        private fun takeFromRing(): ShortArray? {
            val current = AudioBus.soundInput ?: return null
            if (current !== musicInput) return null
            if (current !== input) {
                input = current
                cursor?.close()
                cursor = SoundCursor(current)
            }
            val c = cursor ?: return null
            // 16-bit straight from the native cursor, with the rate these frames were written at.
            val frames = c.take()
            val rate = c.sampleRateHz
            if (frames <= 0 || c.channels != CH || rate <= 0) return null
            return resamplerFor(rate).process(c.pcm, frames)
        }

        private fun resamplerFor(rate: Int): PcmResampler {
            val existing = resampler
            if (existing != null && resamplerInRate == rate) return existing
            existing?.close()
            return PcmResampler(rate, CastEncoder.AUDIO_RATE_HZ, CH).also {
                resampler = it
                resamplerInRate = rate
            }
        }

        override fun close() {
            cursor?.close()
            cursor = null
            resampler?.close()
            resampler = null
        }

        private companion object {
            const val CH = CastEncoder.CHANNELS
            const val HELD = 1

            // A step's result, as GEODE_CAST_AUDIO_STEP lays it out.
            const val EVICT = 0
            const val SKIP = 1
            const val KEEP = 2
            const val TAKE = 3
            const val PAD = 4
            const val SILENCE: Short = 0
        }
    }

    /** No Wi-Fi or Ethernet network with an IPv4 address: the TV has nothing to fetch the stream from. */
    class NoLanAddress : IllegalStateException("no Wi-Fi or Ethernet address to serve the TV from")

    companion object {
        const val TAG = "CastStream"

        /** Segments arrive every second; five without one is a frozen picture on the TV. */
        const val FROZEN_MS = 5_000L
        const val JOIN_MS = 2_000L

        /** The fallback renderer is freed after the stage has led this long (30 s): it costs scenes and a sound engine. */
        const val FALLBACK_IDLE_NS = 30_000_000_000L

        /** How long a new stream waits for the stage's first copy before the fallback draws (half a second). */
        const val STAGE_FIRST_COPY_NS = 500_000_000L

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
