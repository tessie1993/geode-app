package dev.geode.ui.theme

import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.view.HapticFeedbackConstants
import android.view.View
import androidx.core.content.getSystemService

enum class StoneHapticCue(
    private val minApi: Int,
    private val constant: Int,
    private val fallbackMs: LongArray,
) {
    TAP(minApi = 27, constant = HapticFeedbackConstants.TEXT_HANDLE_MOVE, fallbackMs = longArrayOf(8)),

    CONFIRM(minApi = 30, constant = HapticFeedbackConstants.CONFIRM, fallbackMs = longArrayOf(12, 34, 8)),

    DESTRUCTIVE(minApi = 30, constant = HapticFeedbackConstants.REJECT, fallbackMs = longArrayOf(18, 28, 18)),

    SLIDER_TICK(minApi = 34, constant = HapticFeedbackConstants.SEGMENT_FREQUENT_TICK, fallbackMs = longArrayOf(4)),
    ;

    internal fun isAvailable(): Boolean = Build.VERSION.SDK_INT >= minApi

    internal fun platformConstant(): Int = constant

    internal fun waveform(): VibrationEffect = VibrationEffect.createWaveform(longArrayOf(0) + fallbackMs, -1)
}

/**
 * Settings › Look › "Vibrate on touch", set by the theme. Process-wide rather than a composition
 * local because the cues are fired from click lambdas through a [View], dialogs' views included.
 */
object StoneHaptics {
    @Volatile
    var enabled: Boolean = true
}

fun View.performStoneHaptic(cue: StoneHapticCue) {
    if (!StoneHaptics.enabled) return
    if (cue.isAvailable()) {
        performHapticFeedback(cue.platformConstant())
        return
    }
    val vibrator = context.getSystemService<Vibrator>() ?: return
    if (vibrator.hasVibrator()) vibrator.vibrate(cue.waveform())
}
