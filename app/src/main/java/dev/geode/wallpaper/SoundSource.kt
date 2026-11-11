package dev.geode.wallpaper

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.core.content.ContextCompat
import dev.geode.audio.AudioBus
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.bridge.GeodeNative

/**
 * What the wallpaper hears, and whether it is hearing anything at all.
 *
 * Two sources, in order:
 * 1. The app's own ring ([AudioBus.soundInput]) while something feeds it: MuzViz playing, or the
 *    app's mic or capture mode. It is the full-quality signal, so it always wins.
 * 2. Otherwise, with the user's say-so, [OutputMixCapture]: whatever any other app is playing.
 *
 * Which of the two, and when to start or stop the capture, is native (`core/sound/SoundSource`,
 * ADR 0003): each poll hands [GeodeNative.soundSourceStep] the ring's written-frame counter, whether
 * capture is permitted and whether it runs, and gets back the source and what to do with the
 * capture, with hysteresis so a ring fed in bursts never flaps it. This class only starts and stops
 * the [OutputMixCapture] it is told to.
 *
 * It also keeps the silence clock behind "rest when silent": one timestamp, the last time sound
 * was heard. The rule itself is native (`core/sound/Silence`, ADR 0003): each poll has the ring judge its
 * newest samples ([SampleRing.heardAt]) on their level, not on whether a
 * player is running, and [GeodeNative.silenceResting] says when the quiet has lasted long enough
 * to rest. Sound coming back wakes it on the next poll.
 *
 * Polled on the main thread at [POLL_MS] only while [start]ed, which the engine does only while
 * the wallpaper is visible: a hidden wallpaper polls nothing and holds no output-mix capture.
 * One poll reads [LEVEL_FRAMES] frames natively, a few microseconds every quarter second.
 */
class SoundSource(
    private val context: Context,
    /** Called on the main thread when [silent] flips, so the engine can restate its run mode. */
    private val onSilenceChanged: () -> Unit,
) {
    private val capture = OutputMixCapture()
    private val handler = Handler(Looper.getMainLooper())

    /** The input the renderer should read this frame. Written on main, read on the GL thread. */
    @Volatile var input: SampleRing? = AudioBus.soundInput
        private set

    var silent: Boolean = false
        private set

    private var running = false
    private var hearOtherApps = false

    /** The source rule's state, kept here and handed to native each poll (ADR 0003). */
    private val sourceState = LongArray(GeodeNative.SOUND_SOURCE_STATE_LONGS)

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
        // A fresh rule on the next start: the ring gets its grace again before the capture opens.
        sourceState.fill(0L)
    }

    private fun tick() {
        val now = SystemClock.uptimeMillis()
        val app = AudioBus.soundInput
        val answer =
            GeodeNative.soundSourceStep(
                sourceState,
                now,
                ringPresent = app != null,
                ringId = app?.handle ?: 0L,
                ringWritten = app?.writtenFrames ?: 0L,
                capturePermitted = hearOtherApps && (capture.running || hasRecordPermission()),
                captureRunning = capture.running,
            )
        if ((answer and GeodeNative.SOUND_SOURCE_STOP_CAPTURE) != 0) capture.stop()
        if ((answer and GeodeNative.SOUND_SOURCE_START_CAPTURE) != 0 && !capture.start(context)) {
            GeodeNative.soundSourceStartFailed(sourceState, now)
        }
        val useCapture = (answer and GeodeNative.SOUND_SOURCE_USE_CAPTURE) != 0
        val chosen = if (useCapture && capture.running) capture.soundInput else app
        input = chosen
        if (chosen != null) soundAtMs = heardAt(chosen, now)
        val nowSilent = GeodeNative.silenceResting(now, soundAtMs)
        if (nowSilent != silent) {
            silent = nowSilent
            onSilenceChanged()
        }
    }

    private fun hasRecordPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

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
        return if (fresh) ring.heardAt(LEVEL_FRAMES, now, soundAtMs) else soundAtMs
    }

    private companion object {
        const val POLL_MS = 250L

        const val LEVEL_FRAMES = 1024
    }
}
