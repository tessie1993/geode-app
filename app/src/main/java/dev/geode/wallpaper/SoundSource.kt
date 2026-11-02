package dev.geode.wallpaper

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import dev.geode.audio.AudioBus
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audio.SoundInput
import dev.geode.engine.bridge.GeodeNative

/**
 * What the wallpaper hears, and whether it is hearing anything at all.
 *
 * Two sources, in order:
 * 1. The app's own ring ([AudioBus.soundInput]) while something feeds it: MuzViz playing, or the
 *    app's mic or capture mode. It is the full-quality signal, so it always wins.
 * 2. Otherwise, with the user's say-so, [OutputMixCapture]: whatever any other app is playing.
 *
 * It also keeps the silence clock behind "rest when silent": one timestamp, the last time sound
 * was heard. The rule itself is native (`core/sound/Silence`, ADR 0003): each poll hands the newest
 * samples to [GeodeNative.silenceHeardAt], which judges them on their level, not on whether a
 * player is running, and [GeodeNative.silenceResting] says when the quiet has lasted long enough
 * to rest. Sound coming back wakes it on the next poll.
 *
 * Polled on the main thread at [POLL_MS] only while [start]ed, which the engine does only while
 * the wallpaper is visible: a hidden wallpaper polls nothing and holds no output-mix capture.
 * One poll copies [LEVEL_FRAMES] floats per channel, a few microseconds every quarter second.
 */
class SoundSource(
    private val context: Context,
    /** Called on the main thread when [silent] flips, so the engine can restate its run mode. */
    private val onSilenceChanged: () -> Unit,
) {
    private val capture = OutputMixCapture()
    private val handler = Handler(Looper.getMainLooper())
    private val window = Array(2) { FloatArray(LEVEL_FRAMES) }

    /** The input the renderer should read this frame. Written on main, read on the GL thread. */
    @Volatile var input: SoundInput? = AudioBus.soundInput
        private set

    var silent: Boolean = false
        private set

    private var running = false
    private var hearOtherApps = false

    private var captureRetryAtMs = 0L

    private var appRing: SampleRing? = null
    private var appWritten = -1L
    private var appFedAtMs = 0L

    private var heardRing: SampleRing? = null
    private var heardWritten = -1L
    private var soundAtMs = 0L

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
        soundAtMs = SystemClock.uptimeMillis()
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
        val now = SystemClock.uptimeMillis()
        val app = AudioBus.soundInput
        val appFed = app != null && appIsFed(app.ring, now)
        when {
            appFed || !hearOtherApps -> capture.stop()
            capture.running || now < captureRetryAtMs -> Unit
            // A refusal (no permission yet, a device that keeps the output mix to itself) is not
            // retried every poll: that would create and release an audio effect four times a second.
            !capture.start(context) -> captureRetryAtMs = now + CAPTURE_RETRY_MS
        }
        val chosen = if (!appFed && capture.running) capture.soundInput else app
        input = chosen
        if (chosen != null) soundAtMs = heardAt(chosen.ring, now)
        val nowSilent = GeodeNative.silenceResting(now, soundAtMs)
        if (nowSilent != silent) {
            silent = nowSilent
            onSilenceChanged()
        }
    }

    private fun appIsFed(
        ring: SampleRing,
        now: Long,
    ): Boolean {
        if (ring !== appRing) {
            appRing = ring
            appWritten = ring.writtenFrames
            appFedAtMs = 0L
        }
        val written = ring.writtenFrames
        if (written != appWritten) {
            appWritten = written
            appFedAtMs = now
        }
        return appFedAtMs > 0L && now - appFedAtMs < FED_WINDOW_MS
    }

    /**
     * The newest [LEVEL_FRAMES] of [ring], only when it has new samples since the last poll, handed
     * to the native rule; no new samples counts as nothing heard.
     */
    private fun heardAt(
        ring: SampleRing,
        now: Long,
    ): Long {
        val written = ring.writtenFrames
        val fresh = ring === heardRing && written != heardWritten
        heardRing = ring
        heardWritten = written
        val frames = if (fresh && ring.snapshotLatest(window)) LEVEL_FRAMES else 0
        return GeodeNative.silenceHeardAt(window[0], window[1], frames, now, soundAtMs)
    }

    private companion object {
        const val POLL_MS = 250L

        /** The app's ring counts as fed for this long after its last write (a tap writes every few ms). */
        const val FED_WINDOW_MS = 1_000L
        const val CAPTURE_RETRY_MS = 10_000L
        const val LEVEL_FRAMES = 1024
    }
}
