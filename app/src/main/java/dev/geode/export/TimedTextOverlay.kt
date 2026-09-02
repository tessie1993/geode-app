package dev.geode.export

import android.text.SpannableString
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.StaticOverlaySettings
import androidx.media3.effect.TextOverlay
import dev.geode.editor.SubtitleCue

/**
 * Captions that change with time: the cue spanning the frame, or a blank when none does. Cue times
 * are from the clip's first frame; the first timestamp seen is that origin.
 */
@UnstableApi
class TimedTextOverlay(
    private val cues: List<SubtitleCue>,
) : TextOverlay() {
    private val settings =
        StaticOverlaySettings
            .Builder()
            .setBackgroundFrameAnchor(0f, -0.82f)
            .setOverlayFrameAnchor(0f, -1f)
            .build()
    private var originUs = -1L

    override fun getText(presentationTimeUs: Long): SpannableString {
        if (originUs < 0) originUs = presentationTimeUs
        val atMs = (presentationTimeUs - originUs) / 1000L
        // A zero-width layout cannot become a bitmap, so an empty frame draws one space.
        return SpannableString(cues.firstOrNull { it.spans(atMs) }?.text ?: " ")
    }

    override fun getOverlaySettings(presentationTimeUs: Long) = settings

    companion object {
        /** The cues touching [startMs, endMs), re-based so the clip's first frame is 0. */
        fun forSpan(
            cues: List<SubtitleCue>,
            startMs: Long,
            endMs: Long,
        ): List<SubtitleCue> =
            cues
                .filter { it.endMs > startMs && it.startMs < endMs }
                .map { SubtitleCue((it.startMs - startMs).coerceAtLeast(0L), (it.endMs - startMs).coerceAtMost(endMs - startMs), it.text) }
    }
}
