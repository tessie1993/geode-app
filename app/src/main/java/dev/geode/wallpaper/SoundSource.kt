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
 * 1. The app's own ring ([AudioBus.soundSource]) while it is not resting: MuzViz playing, or the app's
 *    mic or capture mode. It is the full-quality signal, so it always wins.
 * 2. Otherwise, with the user's say-so, [OutputMixCapture]: whatever any other app is playing.
 *
 * Both questions, "is the app's ring fed?" and "is what we hear silent?", are one native answer
 * per ring: [GeodeNative.ingestResting], `core/sound/Silence`'s rule and hold fed by the ring
 * itself (#491). A ring nobody writes rests after the same hold as one that plays silence, and
 * the app, the dream, the edge glow and the capture screen ask the same ring the same way, so they
 * never disagree. Sound coming back wakes it on the next poll.
 *
 * Both are native ingest rings (`core/audio/ingest`); [source] is the chosen one's handle, which the
 * renderer reads through its own native cursor (`geode_viz_set_sound_source`). A change of ring is a
 * restart there, at that ring's own rate.
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

    /** The ring the renderer should read this frame (0: none). Written on main, read on the GL thread. */
    @Volatile var source: Long = AudioBus.soundSource
        private set

    var silent: Boolean = false
        private set

    private var running = false
    private var hearOtherApps = false

    private var captureRetryAtMs = 0L

    private val poll =
        object : Runnable {
            override fun run() {
                tick()
                if (running) handler.postDelayed(this, POLL_MS)
            }
        }

    /**
     * Starts listening. Whether the room is quiet is the ring's answer, not a fresh guess per
     * start, so a wallpaper that comes back into view agrees at once with every other surface.
     */
    fun start(hearOtherApps: Boolean) {
        this.hearOtherApps = hearOtherApps
        if (!hearOtherApps) capture.stop()
        // The user may have just granted the permission in the wallpaper's options: try at once.
        captureRetryAtMs = 0L
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
        val now = SystemClock.uptimeMillis()
        val app = AudioBus.soundSource
        val appFed = !GeodeNative.ingestResting(app, now)
        when {
            appFed || !hearOtherApps -> capture.stop()
            capture.running || now < captureRetryAtMs -> Unit
            // A refusal (no permission yet, a device that keeps the output mix to itself) is not
            // retried every poll: that would create and release an audio effect four times a second.
            !capture.start(context) -> captureRetryAtMs = now + CAPTURE_RETRY_MS
        }
        val chosen = if (!appFed && capture.running) capture.ring else app
        source = chosen
        val nowSilent = GeodeNative.ingestResting(chosen, now)
        if (nowSilent != silent) {
            silent = nowSilent
            onSilenceChanged()
        }
    }

    private companion object {
        const val POLL_MS = 250L
        const val CAPTURE_RETRY_MS = 10_000L
    }
}
