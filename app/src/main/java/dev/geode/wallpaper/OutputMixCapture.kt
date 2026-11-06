package dev.geode.wallpaper

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.audiofx.Visualizer
import androidx.core.content.ContextCompat
import dev.geode.RingLog
import dev.geode.audio.AudioBus
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import java.nio.ByteBuffer

/**
 * Whatever any app is playing, as a native ingest [ring] the wallpaper's renderer can hear.
 *
 * The app's own capture path (`audio/PlaybackCaptureService`) needs a MediaProjection consent
 * and a foreground service, which a wallpaper can neither ask for nor keep running. A
 * [Visualizer] on the output mix (audio session 0) needs neither: the microphone permission,
 * granted once in the app, and `MODIFY_AUDIO_SETTINGS`, which the manifest already holds. It is
 * the platform's own answer for "visualize what is playing", and the wallpaper process is bound
 * by the system with the capabilities a visible wallpaper needs to keep it.
 *
 * What it hands over is coarser than the app's tap: 8-bit mono, and only the newest
 * [Visualizer.getCaptureSize] samples at each capture (about 20 a second). Each capture's bytes
 * go to the ring as they arrive, unsigned 8-bit as the platform gives them (`GEODE_PCM_U8`: the
 * ring converts, no loop here), and the ring's epoch states the mix's true rate, so pitch, key and
 * the band split read correctly and an onset lands on the frame it happens; what is lost is
 * the audio between captures, so tempo reads slow from this source. That is the right way round
 * for a wallpaper, which shows hits and colour far more than it shows bar counts.
 *
 * Main thread only: [start] registers the capture listener there, so captures arrive on the
 * same Looper and the ring has one writer. The renderer's GL thread is its one reader. The ring is
 * the process's one output-mix ring ([AudioBus.outputMixRing]), never freed under a renderer.
 */
class OutputMixCapture {
    val ring: Long get() = AudioBus.outputMixRing

    private var visualizer: Visualizer? = null

    /** The direct buffer the JNI side reads; a capture is copied in whole, never converted here. */
    private var bytes: ByteBuffer = ByteBuffer.allocateDirect(0)

    val running: Boolean get() = visualizer != null

    private val listener =
        object : Visualizer.OnDataCaptureListener {
            override fun onWaveFormDataCapture(
                visualizer: Visualizer?,
                waveform: ByteArray?,
                samplingRate: Int,
            ) {
                val data = waveform ?: return
                val buffer = bytes
                val n = minOf(data.size, buffer.capacity())
                if (n <= 0) return
                buffer.clear()
                buffer.put(data, 0, n)
                // android.media.AudioFormat.ENCODING_PCM_8BIT equals geode_api.h's GEODE_PCM_U8.
                // One byte per mono frame: n bytes are n frames.
                GeodeNative.ingestWrite(ring, buffer, 0, n, AudioFormat.ENCODING_PCM_8BIT)
            }

            override fun onFftDataCapture(
                visualizer: Visualizer?,
                fft: ByteArray?,
                samplingRate: Int,
            ) = Unit
        }

    /**
     * Starts capturing, or returns false when it can't: no permission, or a device that refuses
     * the output mix (some do, and another app can hold it). Safe to call when already running.
     */
    fun start(context: Context): Boolean {
        if (visualizer != null) return true
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            return false
        }
        val created = runCatching { Visualizer(OUTPUT_MIX_SESSION) }
        val v =
            created.getOrElse {
                RingLog.note(TAG, "output mix refused", it)
                return false
            }
        val configured =
            runCatching {
                // The capture size can only change while the effect is disabled.
                v.setEnabled(false)
                val size = Visualizer.getCaptureSizeRange()[1].coerceAtMost(MAX_CAPTURE_FRAMES)
                v.setCaptureSize(size)
                // As played, not normalized: normalized lifts every capture to full scale, so
                // a quiet passage hit as hard as a drop and room noise never read as silence.
                v.setScalingMode(Visualizer.SCALING_MODE_AS_PLAYED)
                val rateHz = (v.samplingRate / MILLIHERTZ_PER_HERTZ).takeIf { it > 0 } ?: FALLBACK_RATE_HZ
                bytes = ByteBuffer.allocateDirect(size)
                // A new run is a new stream, begun here on the writer's thread before its first
                // capture: the renderer's cursor restarts at this rate instead of reading a seam.
                GeodeNative.ingestBeginEpoch(ring, rateHz, 1, AudioFormat.CHANNEL_OUT_MONO)
                v.setDataCaptureListener(listener, Visualizer.getMaxCaptureRate(), true, false)
                v.setEnabled(true)
            }
        if (configured.isFailure) {
            RingLog.note(TAG, "output mix setup failed", configured.exceptionOrNull())
            bestEffort(TAG, "release refused visualizer") { v.release() }
            return false
        }
        visualizer = v
        return true
    }

    fun stop() {
        val v = visualizer ?: return
        visualizer = null
        bestEffort(TAG, "release visualizer") {
            v.setEnabled(false)
            v.release()
        }
    }

    private companion object {
        const val TAG = "WallpaperMix"
        const val OUTPUT_MIX_SESSION = 0
        /** Captures are at most 1024 samples today; the cap only bounds one write. */
        const val MAX_CAPTURE_FRAMES = 4096
        const val FALLBACK_RATE_HZ = 48_000
        const val MILLIHERTZ_PER_HERTZ = 1000
    }
}
