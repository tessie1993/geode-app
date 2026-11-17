package dev.geode.wallpaper

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.audiofx.Visualizer
import androidx.core.content.ContextCompat
import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort

/**
 * Whatever any app is playing, as a native ring the wallpaper's renderer can hear.
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
 * The one exception to native capture: the output mix exists only as the Java [Visualizer], so the shell
 * creates and configures it here and hands it down; a native thread polls its waveform into the ring
 * (`core/audio/ingest/CapturePump`) and no sample reaches Kotlin. Main thread only; that native thread is the
 * ring's one writer and the renderer's GL thread its one reader.
 */
class OutputMixCapture {
    /** The ring handle the renderer reads while this runs (native's output-mix kind): made on the first [start], freed by [release]. */
    var soundInput = 0L
        private set

    private var visualizer: Visualizer? = null
    private var handle = 0L

    val running: Boolean get() = visualizer != null

    /**
     * Starts capturing, or returns false when it can't: no permission, or a device that refuses
     * the output mix (some do, and another app can hold it). Safe to call when already running.
     */
    fun start(context: Context): Boolean {
        if (visualizer != null) return true
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            return false
        }
        if (soundInput == 0L) soundInput = GeodeNative.pcmRingCreateKind(GeodeNative.PCM_RING_OUTPUT_MIX)
        if (soundInput == 0L) return false
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
                val size = Visualizer.getCaptureSizeRange()[1].coerceAtMost(GeodeNative.pcmRingMaxWriteFrames(soundInput))
                v.setCaptureSize(size)
                // As played, not normalized: normalized lifts every capture to full scale, so
                // a quiet passage hit as hard as a drop and room noise never read as silence.
                v.setScalingMode(Visualizer.SCALING_MODE_AS_PLAYED)
                v.setEnabled(true)
                // A new run is a new stream: the renderer's cursor restarts instead of reading a seam.
                GeodeNative.pcmRingBeginEpoch(soundInput)
                GeodeNative.javaCaptureVisualizer(v, soundInput, v.captureSize, v.samplingRate, Visualizer.getMaxCaptureRate())
            }
        handle = configured.getOrDefault(0L)
        if (handle == 0L) {
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
        // The poll ends before the effect is released.
        GeodeNative.javaCaptureStop(handle)
        handle = 0L
        bestEffort(TAG, "release visualizer") {
            v.setEnabled(false)
            v.release()
        }
    }

    /** Stops and frees the ring. Main thread. */
    fun release() {
        stop()
        val ring = soundInput
        soundInput = 0L
        GeodeNative.pcmRingDestroy(ring)
    }

    private companion object {
        const val TAG = "WallpaperMix"
        const val OUTPUT_MIX_SESSION = 0
    }
}
