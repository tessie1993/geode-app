package dev.geode.cast

import android.content.Context
import android.content.SharedPreferences
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import dev.geode.RingLog
import dev.geode.audio.AudioBus
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.LookStore
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audio.SoundCursor
import dev.geode.engine.bridge.GeodeNative
import dev.geode.export.EncoderSurface
import dev.geode.export.PcmResampler
import dev.geode.playback.PlaybackEngine
import dev.geode.render.PacedRate
import dev.geode.render.VisualizerRenderer
import dev.geode.util.bestEffort
import java.net.Inet4Address
import java.net.InetAddress
import java.util.concurrent.locks.LockSupport

/**
 * One cast: the phone draws the live look into [CastEncoder] and serves it to the TV.
 *
 * The picture comes from its own [VisualizerRenderer] on its own thread and EGL context, not from
 * the screen's view: casting keeps going with the app in the background and the screen off (owner,
 * 2026-11-02), when the screen's view is paused. It shows what the screen would: the saved live
 * look: the stage's style and params as they change ([CastMirror], previews included), else the
 * saved live state; modulation and the MilkDrop preset (which the renderer restores itself), re-read
 * whenever the app saves them; the same sound ([AudioBus.soundInput]), from which the native renderer
 * also takes MilkDrop's raw PCM itself (core/viz/PcmHandoff), as the screen's does.
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
    private val prefsFiles = GeodePrefsFiles(appContext)
    private val segments = LiveSegments()
    private val server = LiveHttpServer(segments, lanAddress(appContext) ?: throw NoLanAddress())
    private var thread: Thread? = null

    @Volatile
    private var running = false

    @Volatile
    private var lookChanged = true
    private val lookListener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> lookChanged = true }

    /** The URL the receiver loads; valid after [start]. */
    val playlistUrl: String get() = server.playlistUrl

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
        server.stop()
    }

    private fun runRenderLoop() {
        runCatching { renderLoop() }.onFailure {
            RingLog.note(TAG, "render loop: $it")
            if (running) onFailed()
        }
    }

    /** The mirrored look last handed to the renderer; render thread only. */
    private var mirrored: CastMirror.Look? = null

    /** True once segments stopped arriving for [FROZEN_MS]: the encoder or the renderer stalled. */
    val isFrozen: Boolean get() = segments.msSinceLastSegment() > FROZEN_MS

    private fun renderLoop() {
        val encoder = CastEncoder(segments)
        val egl = EncoderSurface(encoder.surface)
        val renderer = VisualizerRenderer(appContext)
        val audio = AudioPump(encoder, musicInput)
        try {
            // The TV plays the ring as written, not the muted phone's sink: no reader waits for the speaker.
            GeodeNative.playoutSetBypass(PlaybackEngine.playout, true)
            egl.makeCurrent()
            renderer.soundInput = { AudioBus.soundInput }
            // The encoder's fixed rate, not the stage's cap: at 30 this renderer is keeping up, so its fluid
            // monitor and the shared thermal trend must not read its frames against the phone's 60 (or 120).
            val castFps = CastEncoder.FPS.toFloat()
            renderer.pacedRate = PacedRate { castFps }
            renderer.onDirectorStyleMoment = CastMirror::directorMoment
            renderer.onSurfaceCreated(null, null)
            renderer.onSurfaceChanged(null, encoder.width, encoder.height)
            val clock = LongArray(GeodeNative.FRAME_CLOCK_STATE)
            GeodeNative.frameClockStart(clock, CastEncoder.FPS, System.nanoTime())
            while (running) {
                LockSupport.parkNanos(GeodeNative.frameClockWaitNs(clock, System.nanoTime()))
                val ptsNs = GeodeNative.frameClockTake(clock, System.nanoTime())
                syncLook(renderer)
                renderer.onDrawFrame(null)
                egl.setPresentationTimeNs(ptsNs)
                egl.swapBuffers()
                audio.pump(untilUs = GeodeNative.frameClockAudioUntilUs(clock))
            }
        } finally {
            GeodeNative.playoutSetBypass(PlaybackEngine.playout, false)
            bestEffort(TAG, "renderer.releaseScenes()") { renderer.releaseScenes() }
            audio.close()
            egl.release()
            encoder.release()
        }
    }

    /** The saved look when the app saves one, then the stage's own look on top while [CastMirror] has one. */
    private fun syncLook(renderer: VisualizerRenderer) {
        if (lookChanged) {
            lookChanged = false
            // The saved live look, read the way the wallpaper and the dream read it.
            renderer.look = LookStore(prefsFiles, appContext.contentResolver).current()
            mirrored = null
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
            encoder.queueAudio(block, need, step[PTS_US])
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
            const val PTS_US = 5
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

        /**
         * This phone's IPv4 address on the Wi-Fi or Ethernet the TV shares, or null. Not simply the active
         * network: with a VPN up, or cellular preferred, that one has no address the TV can reach.
         */
        @Suppress("DEPRECATION") // allNetworks: the one synchronous list; a callback would make start async.
        fun lanAddress(context: Context): InetAddress? {
            val connectivity = context.getSystemService(ConnectivityManager::class.java) ?: return null
            val candidates = (listOfNotNull(connectivity.activeNetwork) + connectivity.allNetworks).distinct()
            return candidates.firstNotNullOfOrNull { lanAddressOf(connectivity, it) }
        }

        private fun lanAddressOf(
            connectivity: ConnectivityManager,
            network: Network,
        ): InetAddress? {
            val caps = connectivity.getNetworkCapabilities(network) ?: return null
            if (caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) return null
            val local = caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) || caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)
            if (!local) return null
            return connectivity
                .getLinkProperties(network)
                ?.linkAddresses
                ?.map { it.address }
                ?.firstOrNull { it is Inet4Address && it.isSiteLocalAddress }
        }
    }
}
