package dev.geode.wallpaper

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.audiofx.Visualizer
import androidx.core.content.ContextCompat
import dev.geode.RingLog
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audio.SoundInput
import dev.geode.util.bestEffort

/**
 * Whatever any app is playing, as a [SoundInput] the wallpaper's renderer can hear.
 *
 * The app's own capture path (`audio/PlaybackCaptureService`) needs a MediaProjection consent
 * and a foreground service, which a wallpaper can neither ask for nor keep running. A
 * [Visualizer] on the output mix (audio session 0) needs neither: the microphone permission,
 * granted once in the app, and `MODIFY_AUDIO_SETTINGS`, which the manifest already holds. It is
 * the platform's own answer for "visualize what is playing", and the wallpaper process is bound
 * by the system with the capabilities a visible wallpaper needs to keep it.
 *
 * What it hands over is coarser than the app's tap: 8-bit mono, and only the newest
 * [Visualizer.getCaptureSize] samples at each capture (about 20 a second). Each capture is
 * written to the ring as it arrives and the ring states the mix's true rate, so pitch, key and
 * the band split read correctly and an onset lands on the frame it happens; what is lost is
 * the audio between captures, so tempo reads slow from this source. That is the right way round
 * for a wallpaper, which shows hits and colour far more than it shows bar counts.
 *
 * Main thread only: [start] registers the capture listener there, so captures arrive on the
 * same Looper and the ring has one writer. The renderer's GL thread is its one reader.
 */
class OutputMixCapture {
    private val ring = SampleRing(RING_FRAMES, channelCount = 2)

    @Volatile private var rateHz = FALLBACK_RATE_HZ

    val soundInput = SoundInput(ring) { rateHz }

    private var visualizer: Visualizer? = null
    private var samples = FloatArray(0)

    val running: Boolean get() = visualizer != null

    private val listener =
        object : Visualizer.OnDataCaptureListener {
            override fun onWaveFormDataCapture(
                visualizer: Visualizer?,
                waveform: ByteArray?,
                samplingRate: Int,
            ) {
                val bytes = waveform ?: return
                val n = minOf(bytes.size, samples.size)
                for (i in 0 until n) samples[i] = ((bytes[i].toInt() and BYTE_MASK) - BYTE_CENTRE) / BYTE_CENTRE.toFloat()
                if (n > 0) ring.write(samples, n, 1)
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
                val size = Visualizer.getCaptureSizeRange()[1].coerceAtMost(ring.maxWriteFrames)
                v.setCaptureSize(size)
                // As played, not normalized: normalized lifts every capture to full scale, so
                // a quiet passage hit as hard as a drop and room noise never read as silence.
                v.setScalingMode(Visualizer.SCALING_MODE_AS_PLAYED)
                rateHz = (v.samplingRate / MILLIHERTZ_PER_HERTZ).takeIf { it > 0 } ?: FALLBACK_RATE_HZ
                samples = FloatArray(size)
                v.setDataCaptureListener(listener, Visualizer.getMaxCaptureRate(), true, false)
                v.setEnabled(true)
            }
        if (configured.isFailure) {
            RingLog.note(TAG, "output mix setup failed", configured.exceptionOrNull())
            bestEffort(TAG, "release refused visualizer") { v.release() }
            return false
        }
        // A new run is a new stream: the renderer's cursor restarts instead of reading a seam.
        ring.beginEpoch()
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
        const val RING_FRAMES = 16_384
        const val FALLBACK_RATE_HZ = 48_000
        const val MILLIHERTZ_PER_HERTZ = 1000
        const val BYTE_MASK = 0xFF
        const val BYTE_CENTRE = 128
    }
}
