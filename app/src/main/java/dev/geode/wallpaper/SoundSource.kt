package dev.geode.wallpaper

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import dev.geode.audio.AudioBus
import dev.geode.engine.bridge.GeodeNative

/**
 * What the wallpaper hears, and whether it is hearing anything at all.
 *
 * Two sources, in order:
 * 1. The app's own ring ([AudioBus.soundSource]) while something feeds it: MuzViz playing, or the
 *    app's mic or capture mode. It is the full-quality signal, so it always wins.
 * 2. Otherwise, with the user's say-so, [OutputMixCapture]: whatever any other app is playing.
 *
 * Both are native ingest rings (`core/audio/ingest`); [source] is the chosen one's handle, which the
 * renderer reads through its own native cursor (`geode_viz_set_sound_source`). A change of ring is a
 * restart there, at that ring's own rate.
 *
 * "Rest when silent" asks the chosen ring ([GeodeNative.ingestResting]: the `core/sound/Silence`
 * rule, fed by the ring itself, so a ring nobody writes counts as silent after the same hold), and
 * a start counts as fresh sound for that hold ([GeodeNative.silenceResting] from the start time), so a
 * wallpaper that comes back into view always moves before it decides the room is quiet. Sound coming
 * back wakes it on the next poll.
 *
 * Polled on the main thread at [POLL_MS] only while [start]ed, which the engine does only while
 * the wallpaper is visible: a hidden wallpaper polls nothing and holds no output-mix capture.
 */
class SoundSource(
    private val context: Context,
    /** Called on the main thread when [silent] flips, so the engine can restate its run mode. */
    private val onSilenceChanged: () -> Unit,
) {
    private val capture = OutputMixCapture()
    private val handler = Handler(Looper.getMainLooper())

    /** The ring the renderer should read this frame, 0 for none. Written on main, read on the GL thread. */
    @Volatile var source: Long = AudioBus.soundSource
        private set

    var silent: Boolean = false
        private set

    private var running = false
    private var hearOtherApps = false

    private var captureRetryAtMs = 0L

    private var appRing = 0L
    private var appWritten = -1L
    private var appFedAtMs = 0L

    private var startedAtMs = 0L

    private val poll =
        object : Runnable {
            override fun run() {
                tick()
                if (running) handler.postDelayed(this, POLL_MS)
            }
        }

    /**
     * Starts listening. Each start counts as fresh sound, so a wallpaper that comes back into
     * view always moves for the grace period before it decides the room is quiet.
     */
    fun start(hearOtherApps: Boolean) {
        this.hearOtherApps = hearOtherApps
        if (!hearOtherApps) capture.stop()
        // The user may have just granted the permission in the wallpaper's options: try at once.
        captureRetryAtMs = 0L
        startedAtMs = SystemClock.uptimeMillis()
        silent = false
        if (running) return
        running = true
        handler.post(poll)
    }

    fun stop() {
        running = false
        handler.removeCallbacks(poll)
        capture.stop()
    }

    private fun tick() {
        // uptimeMillis: the one clock every poller of a ring passes (EdgeGlow, the capture pumps).
        val now = SystemClock.uptimeMillis()
        val app = AudioBus.soundSource
        val appFed = app != 0L && appIsFed(app, now)
        when {
            appFed || !hearOtherApps -> capture.stop()
            capture.running || now < captureRetryAtMs -> Unit
            // A refusal (no permission yet, a device that keeps the output mix to itself) is not
            // retried every poll: that would create and release an audio effect four times a second.
            !capture.start(context) -> captureRetryAtMs = now + CAPTURE_RETRY_MS
        }
        val chosen = if (!appFed && capture.running) capture.ring else app
        source = chosen
        val nowSilent =
            GeodeNative.silenceResting(now, startedAtMs) &&
                (chosen == 0L || GeodeNative.ingestResting(chosen, now))
        if (nowSilent != silent) {
            silent = nowSilent
            onSilenceChanged()
        }
    }

    /** The app's ring counts as fed while its write count moved within [FED_WINDOW_MS] (#491 makes this native). */
    private fun appIsFed(
        ring: Long,
        now: Long,
    ): Boolean {
        val written = GeodeNative.ingestWritten(ring)
        if (ring != appRing) {
            appRing = ring
            appWritten = written
            appFedAtMs = 0L
        }
        if (written != appWritten) {
            appWritten = written
            appFedAtMs = now
        }
        return appFedAtMs > 0L && now - appFedAtMs < FED_WINDOW_MS
    }

    private companion object {
        const val POLL_MS = 250L

        /** The app's ring counts as fed for this long after its last write (a tap writes every few ms). */
        const val FED_WINDOW_MS = 1_000L
        const val CAPTURE_RETRY_MS = 10_000L
    }
}
