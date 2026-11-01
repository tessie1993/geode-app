package dev.geode.render

import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audio.SoundInput
import kotlin.math.abs

/**
 * Settings › Performance › Wallpaper and screensaver: the frame rate the live wallpaper and the
 * dream draw at. Never the in-app live view (that is [PerformanceSettings.fpsCap]) and never an
 * export (which renders offline at its own rate).
 *
 * A home screen is looked at far longer than the app is, mostly with nothing playing, so both
 * choices fall to [idleFps] once the sound stops; they differ only in how smooth the visuals are
 * while music plays. The idle rate is the same for both on purpose: "Smooth" is about motion that
 * follows the music, and with no music there is nothing for the extra frames to follow.
 *
 * [key] is what the store writes, so renaming an entry never resets the user's choice.
 */
enum class AmbientFrameRate(
    val key: String,
    val activeFps: Int,
    val idleFps: Int,
) {
    /** The default: 30 fps while music plays, 15 fps when it stops. */
    BATTERY_SAVER("saver", activeFps = 30, idleFps = 15),

    /** 60 fps while music plays, still 15 fps when it stops. */
    SMOOTH("smooth", activeFps = 60, idleFps = 15),
    ;

    companion object {
        val DEFAULT = BATTERY_SAVER

        fun fromKey(key: String?): AmbientFrameRate = entries.firstOrNull { it.key == key } ?: DEFAULT
    }
}

/**
 * Picks the wallpaper's or the dream's frame rate, frame by frame, from [AmbientFrameRate] and
 * whether anything is audible on the surface's [SoundInput].
 *
 * Called from the frame pacer's sink, on the main thread, so it has to be cheap: while idle it
 * reads one volatile counter a frame ([SampleRing.writtenFrames]) and looks at samples only when
 * that counter moved, which is what makes the ramp back up happen on the very next paced frame
 * once music starts. While active it looks at most every [PROBE_MILLIS]; the drop to idle waits
 * for [IDLE_AFTER_MILLIS] without sound, so a quiet bar or a gap between tracks never dips.
 *
 * "Audible" is two things: the ring is still being written (a paused or stopped player, or no
 * playback session at all, writes nothing), and the newest [PROBE_FRAMES] frames peak above
 * [SILENCE_PEAK] (the mic in a quiet room, or another app's capture with nothing playing, keeps
 * writing silence). Pure Kotlin with no `android.*`, so the rule is tested on the JVM.
 */
class AmbientPacing(
    private val rate: AmbientFrameRate,
    /** Settings › Performance's frame rate cap, the ceiling every live surface keeps to. */
    private val capFps: Int,
    private val input: () -> SoundInput?,
) {
    /** What the surface should draw at now; starts idle, as nothing has been heard yet. */
    var targetFps: Int = idleRate()
        private set

    private var lastHeardMillis = NEVER
    private var lastProbeMillis = NEVER
    private var lastRing: SampleRing? = null
    private var lastWritten = -1L
    private var lastEpoch = -1
    private var scratch: Array<FloatArray> = emptyArray()

    /** Updates and returns [targetFps]. [nowMillis] is any monotonic clock in milliseconds. */
    fun onFrame(nowMillis: Long): Int {
        val idle = targetFps != activeRate()
        if (idle || lastProbeMillis == NEVER || nowMillis - lastProbeMillis >= PROBE_MILLIS) {
            lastProbeMillis = nowMillis
            if (hearing()) lastHeardMillis = nowMillis
        }
        val heardRecently = lastHeardMillis != NEVER && nowMillis - lastHeardMillis < IDLE_AFTER_MILLIS
        targetFps = if (heardRecently) activeRate() else idleRate()
        return targetFps
    }

    private fun activeRate(): Int = minOf(rate.activeFps, capFps)

    private fun idleRate(): Int = minOf(rate.idleFps, activeRate())

    private fun hearing(): Boolean {
        val ring = input()?.ring
        if (ring == null) {
            lastRing = null
            return false
        }
        val written = ring.writtenFrames
        val epoch = ring.epoch
        // A ring seen for the first time only sets the baseline: what it already holds may be a
        // paused track's last second, and reading that as music would hold the active rate for
        // nothing. Real playback moves the count by the next frame.
        val moved = ring === lastRing && (written != lastWritten || epoch != lastEpoch)
        lastRing = ring
        lastWritten = written
        lastEpoch = epoch
        return moved && written > 0L && loud(ring)
    }

    /** Peak of the newest frames; a fresh epoch too short to look at yet counts as sound. */
    private fun loud(ring: SampleRing): Boolean {
        if (scratch.size != ring.channelCount) scratch = Array(ring.channelCount) { FloatArray(PROBE_FRAMES) }
        if (!ring.snapshotLatest(scratch)) return true
        for (channel in scratch) {
            for (sample in channel) {
                if (abs(sample) > SILENCE_PEAK) return true
            }
        }
        return false
    }

    companion object {
        /** How long without sound before the rate drops: longer than any gap between tracks. */
        const val IDLE_AFTER_MILLIS = 2_000L

        /** How often an active surface looks at the samples; an idle one looks every frame. */
        const val PROBE_MILLIS = 250L

        /** About 23 ms at 44.1 kHz: long enough that one zero crossing never reads as silence. */
        const val PROBE_FRAMES = 1024

        /**
         * −60 dBFS: below any music and above digital silence. A mic's noise floor can sit above
         * it, which only keeps the active rate; it never drops a surface that is playing music.
         */
        const val SILENCE_PEAK = 0.001f

        private const val NEVER = Long.MIN_VALUE
    }
}
