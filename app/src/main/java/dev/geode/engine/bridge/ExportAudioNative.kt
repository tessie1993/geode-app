package dev.geode.engine.bridge

// The JNI declarations of geode_export_audio_jni.cpp (CLAUDE.md).
object ExportAudioNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // An export's soundtrack to AAC (core/output/AacTranscode through core/api/geode_export_audio_api.h).

    const val AAC_JOB_RUNNING = 1
    const val AAC_JOB_DONE = 2
    const val AAC_JOB_FAILED = 3

    /** Why [aacJobState] is [AAC_JOB_FAILED]; [aacJobDetail] words it. */
    const val AAC_FAILURE_NO_AUDIO = 1
    const val AAC_FAILURE_CODEC = 2
    const val AAC_FAILURE_STALLED = 3
    const val AAC_FAILURE_NO_FORMAT = 4
    const val AAC_FAILURE_WRITE = 5

    /** The source could not be opened or read at all, as against [AAC_FAILURE_NO_AUDIO]: read, but no audio track. */
    const val AAC_FAILURE_UNREADABLE = 6

    /** Where [aacJobTrack] puts each value. */
    const val AAC_TRACK_RATE = 0
    const val AAC_TRACK_CHANNELS = 1
    const val AAC_TRACK_BIT_RATE = 2
    const val AAC_TRACK_DURATION_US = 3
    const val AAC_TRACK_BYTES = 4
    const val AAC_TRACK_SAMPLES = 5
    const val AAC_TRACK_LONGS = 6

    /** [aacJobSamples] rows: offset in the file, size, pts (us), flags. */
    const val AAC_SAMPLE_LONGS = 4

    /**
     * The track at [fd] ([offset] and [length] its AssetFileDescriptor's span; [length] < 0: the whole descriptor)
     * encoded to AAC from [startMs] for [maxDurationMs] (0: to the end) at [outputRateHz] (0: the AAC rate for its
     * own), each access unit appended to [outFd]. Native takes and closes both descriptors, also when it returns 0.
     * Freed with [aacJobDestroy]; a destroy before the end is the cancel.
     */
    external fun aacJobCreate(
        fd: Int,
        offset: Long,
        length: Long,
        outFd: Int,
        startMs: Long,
        maxDurationMs: Long,
        outputRateHz: Int,
    ): Long

    external fun aacJobDestroy(job: Long)

    /** Steps the job for about [sliceMs] on this thread; true while it runs on. */
    external fun aacJobRun(
        job: Long,
        sliceMs: Int,
    ): Boolean

    external fun aacJobState(job: Long): Int

    external fun aacJobFailure(job: Long): Int

    external fun aacJobDetail(job: Long): String

    /** 0..1 through the range. */
    external fun aacJobProgress(job: Long): Float

    /** Once done, [AAC_TRACK_LONGS] values into [out] by the AAC_TRACK_ indices. */
    external fun aacJobTrack(
        job: Long,
        out: LongArray,
    )

    /** Once done, the sample table, [AAC_SAMPLE_LONGS] per access unit. */
    external fun aacJobSamples(job: Long): LongArray?

    /** Once done, the track's codec-specific data (csd-0); null when there is none. */
    external fun aacJobCsd(job: Long): ByteArray?

    // An export's platform audio work (core/api/geode_export_audio_api.h): blocking, on the caller's thread.

    /**
     * Decodes the finished render at [fd] (detached: native closes it; [offset] and [length] its span, length
     * < 0: the whole descriptor) and reads its loudness for target [target] (0 Leave as-is, 1 YouTube, 2 Shorts).
     * Returns the sentence to word it with (-1: nothing measured) and fills [out] (9 doubles): LUFS, true peak,
     * gain, the gain the level alone needed, resulting LUFS and true peak, the target's ceiling, LU under
     * YouTube worth saying (else 0), peak over YouTube's ceiling (1/0).
     */
    external fun exportLoudnessMeasure(
        fd: Int,
        offset: Long,
        length: Long,
        target: Int,
        out: DoubleArray,
    ): Int
}
