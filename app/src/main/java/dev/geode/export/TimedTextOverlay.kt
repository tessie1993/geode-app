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
 * The native table lives from [forSpan] until Media3 releases the overlay. A frame asked for after a
 * release (Transformer retrying the export) builds it again, and its first frame becomes the origin.
 */
@UnstableApi
class TimedTextOverlay private constructor(
    private val startsMs: LongArray,
    private val endsMs: LongArray,
    private val texts: List<String>,
    private val spanStartMs: Long,
    private val spanEndMs: Long,
    private var table: Long,
) : TextOverlay() {
    private val settings =
        StaticOverlaySettings
            .Builder()
            .setBackgroundFrameAnchor(0f, -0.82f)
            .setOverlayFrameAnchor(0f, -1f)
            .build()

    override fun getText(presentationTimeUs: Long): SpannableString {
        if (table == 0L) table = GeodeNative.cuesForSpan(startsMs, endsMs, spanStartMs, spanEndMs)
        // A zero-width layout cannot become a bitmap, so an empty frame draws one space.
        return SpannableString(texts.getOrNull(GeodeNative.cuesAt(table, presentationTimeUs)) ?: " ")
    }

    override fun getOverlaySettings(presentationTimeUs: Long) = settings

    override fun release() {
        super.release()
        GeodeNative.cuesDestroy(table)
        table = 0L
    }

    companion object {
        /** The captions over [startMs, endMs) of project time, or null when no cue touches it. */
        fun forSpan(
            cues: List<SubtitleCue>,
            startMs: Long,
            endMs: Long,
        ): TimedTextOverlay? {
            val starts = LongArray(cues.size) { cues[it].startMs }
            val ends = LongArray(cues.size) { cues[it].endMs }
            val table = GeodeNative.cuesForSpan(starts, ends, startMs, endMs)
            return if (table == 0L) null else TimedTextOverlay(starts, ends, cues.map(SubtitleCue::text), startMs, endMs, table)
        }
    }
}
