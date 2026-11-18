package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for the screen recorder (CLAUDE.md).
object RecorderNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** Why a recording ended ([recorderStopped]); 0 while it runs. `Stop` in core/output/Recorder. */
    const val RECORD_STOP_USER = 1
    const val RECORD_STOP_MAX_LENGTH = 2
    const val RECORD_STOP_RESIZED = 3
    const val RECORD_STOP_FILE_FULL = 4
    const val RECORD_STOP_FAILED = 5

    /** A record session's state ([recordSessionState]): stopped, the tee should close; then finished, saved or (3) not. */
    const val RECORD_SESSION_CLOSING = 1
    const val RECORD_SESSION_SAVED = 2

    /** The monotonic clock every `nowNs` of a recorder is read from, in ns. */
    external fun recordNowNs(): Long

    /**
     * Record's clock (core/output/Recorder) for a stage of this size: which frames go into the file and
     * their PTS, where each heard sample sits, when it ends. [maxSec] is Settings › Export › Record's
     * maximum and [capMs] the entitlement's cap; 0 is none. 0 when it could not be made.
     */
    external fun recorderCreate(
        stageWidth: Int,
        stageHeight: Int,
        fps: Int,
        maxSec: Int,
        capMs: Long,
    ): Long

    external fun recorderDestroy(handle: Long)

    /** Ends the recording for [why], one of `RECORD_STOP_*`; the first reason wins. */
    external fun recorderStop(
        handle: Long,
        why: Int,
        nowNs: Long,
    )

    /** 0 while it runs, else the `RECORD_STOP_*` it ended for. */
    external fun recorderStopped(handle: Long): Int

    /** The length so far, or the final length once stopped. */
    external fun recorderRecordedMs(
        handle: Long,
        nowNs: Long,
    ): Long

    /** The longest this recording may run, the shorter of its two limits, in ms; 0 for none. */
    external fun recorderLimitMs(handle: Long): Long

    /** Whether a recording [recordedMs] long is kept (a shorter one is a mis-tap). */
    external fun recordWorthKeeping(recordedMs: Long): Boolean

    /**
     * A recording's encoders and file (`geode_record_session_open`): H.264 at the first size the device takes, AAC,
     * and the MP4 muxer on [fd] (read-write; the muxer keeps its own duplicate), on [recorder]'s clock and the
     * session's own encoder thread. 0 when no encoder would start.
     */
    external fun recordSessionOpen(
        recorder: Long,
        fd: Int,
        stageWidth: Int,
        stageHeight: Int,
        shortSide: Int,
        fps: Int,
    ): Long

    /** Once [recordSessionState] is `RECORD_SESSION_SAVED` or `_FAILED` and [StageNative.stageRecordClosed] says the tee is closed. */
    external fun recordSessionDestroy(session: Long)

    /** The encoder's frame into [out]: width, height. */
    external fun recordSessionSize(
        session: Long,
        out: IntArray,
    )

    /** One of `RECORD_SESSION_*`, polled. */
    external fun recordSessionState(session: Long): Int
}
