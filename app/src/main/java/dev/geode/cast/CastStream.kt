package dev.geode.cast

import android.content.Context
import android.content.SharedPreferences
import android.net.ConnectivityManager
import dev.geode.RingLog
import dev.geode.audio.AudioBus
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.LfoStore
import dev.geode.data.PresetStore
import dev.geode.data.SystemMotion
import dev.geode.data.ThemeStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.export.EncoderSurface
import dev.geode.export.PcmResampler
import dev.geode.render.VisualizerRenderer
import dev.geode.util.bestEffort
import java.net.Inet4Address
import java.net.InetAddress

/**
 * One cast: the phone draws the live look into [CastEncoder] and serves it to the TV.
 *
 * The picture comes from its own [VisualizerRenderer] on its own thread and EGL context, not from
 * the screen's view: casting keeps going with the app in the background and the screen off (owner,
 * 2026-11-02), when the screen's view is paused. It shows what the screen would: the saved live
 * look: the stage's style and params as they change ([CastMirror], previews included), else the
 * saved live state; modulation and the MilkDrop preset (which the renderer restores itself), re-read
 * whenever the app saves them; the same sound (whichever native ring the visuals hear, [AudioBus.soundSource]:
 * the player's, or the mic's or capture's while one owns the visuals, read by the native renderer itself), from
 * which the native renderer also takes MilkDrop's raw PCM itself (core/viz/PcmHandoff), as the screen's does.
 *
 * If the render loop dies after the stream started, [onFailed] runs (on the render thread): a
 * frozen picture on the TV with a muted phone is worse than ending the cast.
 *
 * The sound goes to the TV inside the stream (the phone is a silent remote, see [CastController]).
 * Audio is clocked by the wall clock like the picture: each frame queues exactly a frame's worth of
 * 48 kHz audio, from what the ring gained, padded with silence while the music is paused, so the
 * TV's player never waits for audio and picture and sound stay together.
 */
internal class CastStream(
    context: Context,
    /** The player's ingest ring (`geode_ingest*`, 0 for none): only its sound goes to the TV (see [AudioPump]). */
    private val music: Long,
    private val onFailed: () -> Unit,
) {
    private val appContext = context.applicationContext
    private val prefsFiles = GeodePrefsFiles(appContext)
    private val segments = LiveSegments()
    private val server = LiveHttpServer(segments, lanAddress(appContext) ?: error("no Wi-Fi address to serve the TV from"))
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
        val audio = AudioPump(encoder, music)
        try {
            egl.makeCurrent()
            renderer.soundSource = { AudioBus.soundSource }
            renderer.onSurfaceCreated(null, null)
            renderer.onSurfaceChanged(null, encoder.width, encoder.height)
            val startNs = System.nanoTime()
            var frame = 0L
            while (running) {
                syncLook(renderer)
                renderer.onDrawFrame(null)
                val ptsNs = frame * NANOS_PER_SEC / CastEncoder.FPS
                egl.setPresentationTimeNs(ptsNs)
                egl.swapBuffers()
                audio.pump(untilUs = (frame + 1) * MICROS_PER_SEC / CastEncoder.FPS)
                frame++
                val aheadNs = startNs + frame * NANOS_PER_SEC / CastEncoder.FPS - System.nanoTime()
                if (aheadNs > 0) Thread.sleep(aheadNs / NANOS_PER_MS)
            }
        } finally {
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
            restoreLook(renderer)
            mirrored = null
        }
        val look = CastMirror.look ?: return
        if (look === mirrored) return
        mirrored = look
        renderer.requestedSceneId = look.sceneId
        renderer.sceneParams = look.params
    }

    /** The saved live look, read the way the live wallpaper reads it. */
    private fun restoreLook(renderer: VisualizerRenderer) {
        prefsFiles.viz.getString(KEY_LIVE_STATE, null)?.let { json ->
            runCatching { PresetStore.fromJson(json) }.getOrNull()?.let { preset ->
                renderer.requestedSceneId = preset.sceneId
                renderer.sceneParams = preset.params
            }
        }
        val gui = ThemeStore(prefsFiles.general).loadGui()
        renderer.reducedMotion = SystemMotion.isReduced(appContext.contentResolver)
        renderer.soundTuning = gui.soundTuning
        val modulation = LfoStore(prefsFiles.general)
        renderer.lfoEngine.configs = modulation.load()
        renderer.adsrEngine.configs = modulation.loadAdsrs()
        renderer.reactions = modulation.loadReactions()
    }

    /**
     * Turns what the ring gained into exactly a frame's worth of 48 kHz stereo per call: resampled
     * from the source rate, held in a short FIFO, padded with silence when there is too little
     * (paused, between tracks) and trimmed when too much piles up (the ring ran ahead).
     *
     * Only the player's music is sent: it reads the player's own ring ([music]) through a native reader
     * (`geode_ingest_reader`: its own cursor, the downmix and the int16 conversion are native). The mic
     * and other apps' audio write a different ring, so while they play the visuals hear them but the TV
     * gets silence: the room's sound must not leave the phone, and the mic would hear the TV and feed back.
     */
    private class AudioPump(
        private val encoder: CastEncoder,
        music: Long,
    ) : AutoCloseable {
        private var reader = if (music != 0L) GeodeNative.ingestReaderCreate(music) else 0L
        private val pcm = ShortArray(READ_FRAMES * CastEncoder.CHANNELS)
        private val rate = IntArray(1)
        private var resampler: PcmResampler? = null
        private var resamplerInRate = 0
        private val fifo = ShortArray(FIFO_FRAMES * CastEncoder.CHANNELS)
        private var fifoFrames = 0
        private var producedFrames = 0L
        private var block = ShortArray(0)

        fun pump(untilUs: Long) {
            takeFromRing()
            val target = (untilUs * CastEncoder.AUDIO_RATE_HZ / MICROS_PER_SEC)
            val need = (target - producedFrames).toInt()
            if (need <= 0) return
            val samples = need * CastEncoder.CHANNELS
            if (block.size < samples) block = ShortArray(samples)
            val fromFifo = minOf(need, fifoFrames)
            System.arraycopy(fifo, 0, block, 0, fromFifo * CastEncoder.CHANNELS)
            block.fill(SILENCE, fromFifo * CastEncoder.CHANNELS, samples)
            System.arraycopy(fifo, fromFifo * CastEncoder.CHANNELS, fifo, 0, (fifoFrames - fromFifo) * CastEncoder.CHANNELS)
            fifoFrames -= fromFifo
            encoder.queueAudio(block, need, producedFrames * MICROS_PER_SEC / CastEncoder.AUDIO_RATE_HZ)
            producedFrames += need
        }

        /** Everything new, one native block (one rate) at a time; native bounds the catch-up after a stall. */
        private fun takeFromRing() {
            if (reader == 0L) return
            repeat(MAX_READS) {
                val frames = GeodeNative.ingestReaderReadS16(reader, pcm, READ_FRAMES, rate)
                if (frames <= 0 || rate[0] <= 0) return
                val out = resamplerFor(rate[0]).process(pcm, frames)
                append(out, out.size / CastEncoder.CHANNELS)
            }
        }

        private fun resamplerFor(rate: Int): PcmResampler {
            val existing = resampler
            if (existing != null && resamplerInRate == rate) return existing
            existing?.close()
            return PcmResampler(rate, CastEncoder.AUDIO_RATE_HZ, CastEncoder.CHANNELS).also {
                resampler = it
                resamplerInRate = rate
            }
        }

        private fun append(
            pcm: ShortArray,
            frames: Int,
        ) {
            var count = frames
            var from = 0
            if (count > FIFO_FRAMES) {
                from = (count - FIFO_FRAMES) * CastEncoder.CHANNELS
                count = FIFO_FRAMES
            }
            val overflow = fifoFrames + count - FIFO_FRAMES
            if (overflow > 0) {
                System.arraycopy(fifo, overflow * CastEncoder.CHANNELS, fifo, 0, (fifoFrames - overflow) * CastEncoder.CHANNELS)
                fifoFrames -= overflow
            }
            System.arraycopy(pcm, from, fifo, fifoFrames * CastEncoder.CHANNELS, count * CastEncoder.CHANNELS)
            fifoFrames += count
        }

        override fun close() {
            resampler?.close()
            resampler = null
            if (reader != 0L) GeodeNative.ingestReaderDestroy(reader)
            reader = 0L
        }

        private companion object {
            /** Half a second at 48 kHz: more than that and the TV's sound would drift behind its picture. */
            const val FIFO_FRAMES = 24_000
            const val SILENCE: Short = 0

            /** Frames per native read; a block also ends at a rate or epoch change. */
            const val READ_FRAMES = 4096

            /** Native caps the backlog at 2^14 frames; a few more reads cover blocks cut at format changes. */
            const val MAX_READS = 8
        }
    }

    companion object {
        const val TAG = "CastStream"
        const val KEY_LIVE_STATE = "live_state"

        /** Segments arrive every second; five without one is a frozen picture on the TV. */
        const val FROZEN_MS = 5_000L
        const val JOIN_MS = 2_000L
        const val NANOS_PER_SEC = 1_000_000_000L
        const val NANOS_PER_MS = 1_000_000L
        const val MICROS_PER_SEC = 1_000_000L

        /** This phone's IPv4 address on the active network (the Wi-Fi the TV is on), or null. */
        fun lanAddress(context: Context): InetAddress? {
            val connectivity = context.getSystemService(ConnectivityManager::class.java) ?: return null
            val links = connectivity.getLinkProperties(connectivity.activeNetwork) ?: return null
            return links.linkAddresses
                .map { it.address }
                .firstOrNull { it is Inet4Address && it.isSiteLocalAddress }
        }
    }
}
