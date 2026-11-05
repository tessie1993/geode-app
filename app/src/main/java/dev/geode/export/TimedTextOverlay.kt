package dev.geode.export

import android.text.SpannableString
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.StaticOverlaySettings
import androidx.media3.effect.TextOverlay
import dev.geode.editor.SubtitleCue
import dev.geode.engine.bridge.GeodeNative

/**
 * Captions that change with time: the cue spanning the frame, or a blank when none does. Which cues
 * touch the item, their re-basing to its first frame and the per-frame lookup are native
 * (`core/signal/timeline/Cues`, pinned by `core/tests/timeline_test`); only the text is held here.
 *
 * No native handle is kept: [cues] is the item's (start, end, source) triples and [origin] its clock,
 * plain arrays the native lookup reads and steps. Media3 promises neither that it releases an
 * overlay exactly once nor that no frame is asked for afterwards, so a handle freed in `release()`
 * could leak or be read after it was freed; arrays can do neither.
 */
@UnstableApi
class TimedTextOverlay private constructor(
    private val cues: LongArray,
    private val texts: List<String>,
) : TextOverlay() {
    private val settings =
        StaticOverlaySettings
            .Builder()
            .setBackgroundFrameAnchor(0f, -0.82f)
            .setOverlayFrameAnchor(0f, -1f)
            .build()
    private val origin = LongArray(GeodeNative.CUE_ORIGIN_LONGS)

    override fun getText(presentationTimeUs: Long): SpannableString {
        // A zero-width layout cannot become a bitmap, so an empty frame draws one space.
        return SpannableString(texts.getOrNull(GeodeNative.cuesAt(cues, origin, presentationTimeUs)) ?: " ")
    }

    override fun getOverlaySettings(presentationTimeUs: Long) = settings

    companion object {
        /** The captions over [startMs, endMs) of project time, or null when no cue touches it. */
        fun forSpan(
            cues: List<SubtitleCue>,
            startMs: Long,
            endMs: Long,
        ): TimedTextOverlay? {
            val starts = LongArray(cues.size) { cues[it].startMs }
            val ends = LongArray(cues.size) { cues[it].endMs }
            val triples = GeodeNative.cuesForSpan(starts, ends, startMs, endMs)
            return if (triples.isEmpty()) null else TimedTextOverlay(triples, cues.map(SubtitleCue::text))
        }
    }
}
