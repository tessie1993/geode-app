package dev.geode.wallpaper

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.audiofx.Visualizer
import androidx.core.content.ContextCompat
import dev.geode.RingLog
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort

/**
 * Whatever any app is playing, as a [SampleRing] the wallpaper's renderer can hear.
 *
 * The app's own capture path (`audio/PlaybackCaptureService`) needs a MediaProjection consent
 * and a foreground service, which a wallpaper can neither ask for nor keep running. A
 * [Visualizer] on the output mix (audio session 0) needs neither: the microphone permission,
 * granted once in the app, and `MODIFY_AUDIO_SETTINGS`, which the manifest already holds. It is
 * the platform's own answer for "visualize what is playing", and the wallpaper process is bound
 * by the system with the capabilities a visible wallpaper needs to keep it.
 *
 * What it hands over is coarser than the app's tap: 8-bit mono, and only the newest
 * [Visualizer.getCaptureSize] samples at each capture (about 20 a second, ~21 ms of every 50).
 * Those are snapshots, not a stream, and the ring is told so: each capture is written as it
 * arrives, its bytes as they are, with the mix's true rate, and native stitches it onto the
 * capture clock (`core/audio/ingest/SnapshotStitch`), so the engine's sample clock (tempo, bars,
 * loudness) is real time and no two snapshots meet at a seam. What falls between captures is lost.
 *
 * Main thread only: [start] registers the capture listener there, so captures arrive on the
 * same Looper and the ring has one writer. The renderer's GL thread is its one reader.
 */
class OutputMixCapture {
    /** What the renderer reads while this runs. Freed with this object, once no renderer holds it. */
    val soundInput = SampleRing(RING_FRAMES, channelCount = 2)

    init {
        if (!GeodeNative.pcmRingSetSnapshots(soundInput.handle, true)) RingLog.note(TAG, "no snapshot stitch")
    }

    private var rateHz = FALLBACK_RATE_HZ

    private var visualizer: Visualizer? = null
    private var captureSize = 0

    val running: Boolean get() = visualizer != null

    private val listener =
        object : Visualizer.OnDataCaptureListener {
            override fun onWaveFormDataCapture(
                visualizer: Visualizer?,
                waveform: ByteArray?,
                samplingRate: Int,
            ) {
                val bytes = waveform ?: return
                val n = minOf(bytes.size, captureSize)
                if (n > 0) soundInput.write(bytes, n, AudioFormat.ENCODING_PCM_8BIT, 1, rateHz)
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
                val size = Visualizer.getCaptureSizeRange()[1].coerceAtMost(soundInput.maxWriteFrames)
                v.setCaptureSize(size)
                // As played, not normalized: normalized lifts every capture to full scale, so
                // a quiet passage hit as hard as a drop and room noise never read as silence.
                v.setScalingMode(Visualizer.SCALING_MODE_AS_PLAYED)
                rateHz = (v.samplingRate / MILLIHERTZ_PER_HERTZ).takeIf { it > 0 } ?: FALLBACK_RATE_HZ
                captureSize = size
                v.setDataCaptureListener(listener, Visualizer.getMaxCaptureRate(), true, false)
                v.setEnabled(true)
            }
        if (configured.isFailure) {
            RingLog.note(TAG, "output mix setup failed", configured.exceptionOrNull())
            bestEffort(TAG, "release refused visualizer") { v.release() }
            return false
        }
        // A new run is a new stream: the renderer's cursor restarts instead of reading a seam.
        soundInput.beginEpoch()
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

        // Its runway (a quarter) is the longest gap between captures the native stitch fills: 170 ms at 48 kHz.
        const val RING_FRAMES = 32_768
        const val FALLBACK_RATE_HZ = 48_000
        const val MILLIHERTZ_PER_HERTZ = 1000
    }
}
