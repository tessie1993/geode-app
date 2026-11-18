package dev.geode.engine.bridge

// The JNI declarations of geode_ingest_jni.cpp (CLAUDE.md).
object IngestNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /**
     * Live input as a native input stream writing the process's mic ring (`core/audio/ingest/MicInput`); only
     * once RECORD_AUDIO is granted. [deviceRateHz] goes first (0 unknown); [unprocessed] when the device supports it.
     */
    external fun micStart(
        deviceRateHz: Int,
        unprocessed: Boolean,
    ): Boolean

    /** Stops live input; no block is written after it returns. */
    external fun micStop()

    /** False after [micStop], or once the stream closed and could not be opened again. */
    external fun micActive(): Boolean

    /**
     * Another app's audio, built, read and released natively (`core/audio/ingest/CapturePump`): an `AudioRecord`
     * over [projection], the `MediaProjection` the user consented to, writing [ring]. 0 when none would record.
     */
    external fun javaCapturePlayback(
        projection: Any,
        ring: Long,
    ): Long

    /** The output mix (a `Visualizer` on session 0), built, read and released natively into [ring]. 0 when refused. */
    external fun javaCaptureOutputMix(ring: Long): Long

    /** Stops and frees a [javaCapturePlayback] or [javaCaptureOutputMix] and its Java object; no write follows. Takes 0. */
    external fun javaCaptureStop(handle: Long)

    /** False after stop, or once the capture ended itself (a read error). */
    external fun javaCaptureActive(handle: Long): Boolean

    /** Running and only digital zero for 4 s (`core/sound/Silence`): a capture the playing app refuses. */
    external fun javaCaptureQuiet(handle: Long): Boolean

    // AIFF files (core/audio/ingest/AiffReader), for the player's extractor.

    /** What [aiffScanStep] answers: feed it again, the header is read, or this is not an AIFF that plays. */
    const val AIFF_SCAN_MORE = 0
    const val AIFF_SCAN_DONE = 1
    const val AIFF_SCAN_INVALID = -1

    /** The most [aiffScanRead] asks for at once. */
    const val AIFF_SCAN_MAX_READ = 22

    /** Where [aiffScanHeader] puts each value. */
    const val AIFF_HEADER_CHANNELS = 0
    const val AIFF_HEADER_SAMPLE_RATE = 1
    const val AIFF_HEADER_PCM = 2
    const val AIFF_HEADER_BYTES_PER_FRAME = 3
    const val AIFF_HEADER_FRAMES = 4
    const val AIFF_HEADER_DATA_START = 5
    const val AIFF_HEADER_DATA_BYTES = 6
    const val AIFF_HEADER_COUNT = 7

    /** The header's `AIFF_HEADER_PCM`: how the samples lie, 0 for a layout the player has no encoding for. */
    const val AIFF_PCM_S16_LE = 1
    const val AIFF_PCM_S16_BE = 2
    const val AIFF_PCM_S24_BE = 3
    const val AIFF_PCM_S32_BE = 4

    /**
     * The AIFF header walk for a caller that holds the file itself (the player's extractor): skip
     * [aiffScanSkip] bytes, read [aiffScanRead] bytes, hand them to [aiffScanStep], and repeat while it
     * answers [AIFF_SCAN_MORE]. Freed with [aiffScanDestroy].
     */
    external fun aiffScanCreate(): Long

    external fun aiffScanDestroy(handle: Long)

    external fun aiffScanSkip(handle: Long): Long

    external fun aiffScanRead(handle: Long): Int

    /** The first [n] bytes of [bytes], which must be the count [aiffScanRead] asked for. */
    external fun aiffScanStep(
        handle: Long,
        bytes: ByteArray,
        n: Int,
    ): Int

    /** Fills [out] (`AIFF_HEADER_COUNT` values); false before [AIFF_SCAN_DONE]. */
    external fun aiffScanHeader(
        handle: Long,
        out: LongArray,
    ): Boolean

    /** The start time of the frame [bytes] into the samples lies in. */
    external fun aiffTimeUs(
        bytes: Long,
        bytesPerFrame: Int,
        sampleRate: Int,
    ): Long

    /** The byte offset into the samples of the frame playing at [timeUs], held inside [dataBytes]. */
    external fun aiffSeekOffset(
        timeUs: Long,
        bytesPerFrame: Int,
        sampleRate: Int,
        dataBytes: Long,
    ): Long

    /** The most [aiffReadBytes] hands the player's extractor at once. */
    const val AIFF_MAX_SAMPLE_BYTES = 32 * 1024

    /** The bytes to pass over at [position] to reach the first sample at [dataStart]; 0 on or past it. */
    external fun aiffGapBytes(
        position: Long,
        dataStart: Long,
    ): Long

    /** The most to read at [position]; 0 once the [dataBytes] of samples from [dataStart] are done. */
    external fun aiffReadBytes(
        position: Long,
        dataStart: Long,
        dataBytes: Long,
    ): Int

    /** Of [pending] bytes held, those in whole frames; a part frame waits. */
    external fun aiffWholeBytes(
        pending: Int,
        bytesPerFrame: Int,
    ): Int

    /** The start time of the [pending] bytes held, which end at [position]. */
    external fun aiffPendingTimeUs(
        position: Long,
        dataStart: Long,
        pending: Int,
        bytesPerFrame: Int,
        sampleRate: Int,
    ): Long

    // The PCM ingest ring (core/audio/ingest/PcmRing): one lock-free ring per audio source. Every write
    // carries its rate, and the int16 and 8-bit conversions to float happen inside it. Kinds and sizes are
    // native (core/audio/ingest/RingKinds).
    const val PCM_RING_PLAYER: Int = 0
    const val PCM_RING_MIC: Int = 1
    const val PCM_RING_OUTPUT_MIX: Int = 2

    /** The process's ring of [kind] ([PCM_RING_PLAYER], or [PCM_RING_MIC] layered over it), never freed; 0 for another kind. */
    external fun pcmRingShared(kind: Int): Long

    /** A new ring of [kind] ([PCM_RING_OUTPUT_MIX]), its owner's to free with [pcmRingDestroy]; 0 without memory. */
    external fun pcmRingCreateKind(kind: Int): Long

    /** Drops the handle; the ring itself goes once no native cursor reads it. Never while a thread writes or a stage holds it. */
    external fun pcmRingDestroy(handle: Long)

    /** The most frames one write publishes (the runway). */
    external fun pcmRingMaxWriteFrames(handle: Long): Int

    /**
     * The player's tap writing [handle]: the whole frames in [bytes] bytes of [buffer] from [position], in the format
     * of the last [pcmTapFlush], where they lie (a heap buffer from its array). Returns the frames written, 0 with
     * the gate closed ([pcmTapSetOwner]), before a flush, for a read-only heap buffer or an encoding it does not read.
     */
    external fun pcmTapWrite(
        handle: Long,
        buffer: java.nio.ByteBuffer,
        position: Int,
        bytes: Int,
    ): Int

    /**
     * The chain flushed with this format (`AudioFormat.ENCODING_PCM_16BIT`, `_8BIT` or `_FLOAT` for [encoding]):
     * native starts a ring epoch while the gate is open, then keeps the format for [pcmTapWrite]. The playback thread.
     */
    external fun pcmTapFlush(
        handle: Long,
        sampleRate: Int,
        channels: Int,
        encoding: Int,
    )

    /** Opens or closes the tap's gate: closed while a capture of another app writes the ring. Open by default. */
    external fun pcmTapSetOwner(
        handle: Long,
        owns: Boolean,
    )

    /** A new stream: every reader restarts at its next take. Any thread. */
    external fun pcmRingBeginEpoch(handle: Long)

    /** Frames written since the last [pcmRingBeginEpoch]. */
    external fun pcmRingWritten(handle: Long): Long

    /** Frames ever written, across epochs: the position [PlayoutNative.playoutRingSpan] counts ring samples in. */
    external fun pcmRingHead(handle: Long): Long

    /**
     * The playout clock ([PlayoutNative.playoutCreate]) of the sink that plays this ring, 0 for none: a renderer fed the
     * ring then hears up to the sample the speaker plays, not the newest. The clock must outlive the ring.
     */
    external fun pcmRingSetPlayout(
        handle: Long,
        playout: Long,
    )

    /**
     * The rest-when-silent rule over the ring's newest [frames] frames: [nowMs] when any is above about
     * -42 dBFS, else [heardAtMs] (also when the ring has not written that many).
     */
    external fun pcmRingHeardAt(
        handle: Long,
        frames: Int,
        nowMs: Long,
        heardAtMs: Long,
    ): Long
}
