package dev.geode.audio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import androidx.core.content.ContextCompat
import dev.geode.RingLog
import dev.geode.engine.bridge.IngestNative

/**
 * Whatever any app is playing, as a native ring a renderer can hear: the app's "hear other apps" and the
 * screen saver both use it.
 *
 * The app's own capture path (`audio/PlaybackCaptureService`) needs a MediaProjection consent
 * and a foreground service, which a dream can neither ask for nor keep running. A
 * `Visualizer` on the output mix (audio session 0) needs neither: the microphone permission,
 * granted once in the app, and `MODIFY_AUDIO_SETTINGS`, which the manifest already holds. It is
 * the platform's own answer for "visualize what is playing".
 *
 * What it hands over is coarser than the app's tap: 8-bit mono, and only the newest
 * `Visualizer.getCaptureSize` samples at each capture (about 20 a second, ~21 ms of every 50).
 * Those are snapshots, not a stream, and the ring is told so: each capture is written as it
 * arrives, its bytes as they are, with the mix's true rate, and native stitches it onto the
 * capture clock (`core/audio/ingest/SnapshotStitch`), so the engine's sample clock (tempo, bars,
 * loudness) is real time and no two snapshots meet at a seam. What falls between captures is lost.
 *
 * Native builds the `Visualizer` (session 0, as played), polls its waveform into the ring on its own thread
 * and releases it (`core/audio/ingest/CapturePump`); this class only checks the permission and says start and
 * stop. Main thread only; that native thread is the ring's one writer and the renderer's GL thread its one reader.
 */
class OutputMixCapture {
    /** The ring handle the renderer reads while this runs (native's output-mix kind): made on the first [start], freed by [release]. */
    var soundInput = 0L
        private set

    private var handle = 0L

    val running: Boolean get() = handle != 0L

    /**
     * Starts capturing, or returns false when it can't: no permission, or a device that refuses
     * the output mix (some do, and another app can hold it). Safe to call when already running.
     */
    fun start(context: Context): Boolean {
        if (handle != 0L) return true
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            return false
        }
        if (soundInput == 0L) soundInput = IngestNative.pcmRingCreateKind(IngestNative.PCM_RING_OUTPUT_MIX)
        if (soundInput == 0L) return false
        handle = IngestNative.javaCaptureOutputMix(soundInput)
        if (handle == 0L) RingLog.note(TAG, "output mix refused")
        return handle != 0L
    }

    /** The poll ends and the effect is released before this returns. */
    fun stop() {
        IngestNative.javaCaptureStop(handle)
        handle = 0L
    }

    /** Stops and frees the ring. Main thread. */
    fun release() {
        stop()
        val ring = soundInput
        soundInput = 0L
        IngestNative.pcmRingDestroy(ring)
    }

    private companion object {
        const val TAG = "OutputMix"
    }
}
