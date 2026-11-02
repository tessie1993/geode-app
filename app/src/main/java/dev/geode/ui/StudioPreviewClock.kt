package dev.geode.ui

import android.os.SystemClock
import dev.geode.engine.bridge.GeodeNative
import java.io.Closeable

/**
 * The Studio preview's programme clock: a handle on `core/studio/PreviewClock`, where the timing
 * maths lives (ADR 0003). This class only passes values across: the monotonic time it reads here is
 * the one the native clock runs on, so the caller never deals in nanoseconds.
 *
 * Used from the main thread only, like the players it follows. [close] it.
 */
internal class StudioPreviewClock : Closeable {
    private var handle: Long = GeodeNative.previewCreate()
    private val offset = LongArray(1)

    /** The audio lane's clips in programme time, in start order: (start, duration) in ms. */
    fun setSpans(spans: List<Pair<Long, Long>>) {
        if (handle == 0L) return
        GeodeNative.previewSetSpans(
            handle,
            LongArray(spans.size) { spans[it].first },
            LongArray(spans.size) { spans[it].second },
        )
    }

    fun setRate(
        fps: Int,
        lengthMs: Long,
    ) {
        if (handle != 0L) GeodeNative.previewSetRate(handle, fps, lengthMs)
    }

    fun play(programmeMs: Long) {
        if (handle != 0L) GeodeNative.previewPlay(handle, programmeMs, nowNs())
    }

    fun pause() {
        if (handle != 0L) GeodeNative.previewPause(handle, nowNs())
    }

    fun seek(programmeMs: Long) {
        if (handle != 0L) GeodeNative.previewSeek(handle, programmeMs, nowNs())
    }

    /** The audio player is [spanPosMs] into span [span]. */
    fun heard(
        span: Int,
        spanPosMs: Long,
    ) {
        if (handle != 0L) GeodeNative.previewAudio(handle, span, spanPosMs, nowNs())
    }

    /** Programme time now, on a frame boundary. */
    fun nowMs(): Long = if (handle == 0L) 0L else GeodeNative.previewNow(handle, nowNs())

    fun ended(): Boolean = handle != 0L && GeodeNative.previewEnded(handle, nowNs())

    /** The span under [programmeMs] and the offset into it, or null in a gap. */
    fun locate(programmeMs: Long): Pair<Int, Long>? {
        if (handle == 0L) return null
        val span = GeodeNative.previewLocate(handle, programmeMs, offset)
        return if (span < 0) null else span to offset[0]
    }

    override fun close() {
        if (handle != 0L) GeodeNative.previewDestroy(handle)
        handle = 0L
    }

    companion object {
        /** The start of the frame [programmeMs] falls on at [fps]: what a paused preview shows. */
        fun frameOf(
            programmeMs: Long,
            fps: Int,
        ): Long = GeodeNative.previewFrameOf(programmeMs, fps)

        private fun nowNs(): Long = SystemClock.elapsedRealtimeNanos()
    }
}
