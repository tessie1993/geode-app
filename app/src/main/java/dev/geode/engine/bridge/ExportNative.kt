package dev.geode.engine.bridge

// The JNI declarations of geode_export_jni.cpp, and the export-pass and watermark calls geode_viz_jni.cpp carries.
object ExportNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** Fields of the long[7] [loopSpecOf] and [loopSpecDerive] fill. */
    const val LOOP_SPEC_LOOP_MS = 0
    const val LOOP_SPEC_CROSSFADE_MS = 1
    const val LOOP_SPEC_FPS = 2
    const val LOOP_SPEC_LOOP_FRAMES = 3
    const val LOOP_SPEC_CROSSFADE_FRAMES = 4
    const val LOOP_SPEC_SOURCE_FRAMES = 5
    const val LOOP_SPEC_LOOP_DURATION_US = 6
    const val LOOP_SPEC_LONGS = 7

    /** Fields of the long[6] [loopLimits] returns. */
    const val LOOP_LIMIT_MIN_LOOP_MS = 0
    const val LOOP_LIMIT_MAX_LOOP_MS = 1
    const val LOOP_LIMIT_MIN_CROSSFADE_MS = 2
    const val LOOP_LIMIT_MAX_CROSSFADE_MS = 3
    const val LOOP_LIMIT_DEFAULT_CROSSFADE_MS = 4
    const val LOOP_LIMIT_MAX_DRIFT_STOPS = 5

    /** The ranges a loop render takes (`core/output/LoopPlan`), in LOOP_LIMIT_* order. */
    external fun loopLimits(): LongArray

    /** Loose UI numbers clamped into a valid loop spec, written to [out] (LOOP_SPEC_* order). */
    external fun loopSpecOf(
        loopMs: Long,
        crossfadeMs: Long,
        fps: Int,
        out: LongArray,
    )

    /** The derived fields of the spec as given, no clamping, into [out] (LOOP_SPEC_* order). */
    external fun loopSpecDerive(
        loopMs: Long,
        crossfadeMs: Long,
        fps: Int,
        out: LongArray,
    )

    /** Every repeat of a long-form loop as (offset µs, loop index) pairs. */
    external fun loopExtendPlan(
        loopUs: Long,
        totalUs: Long,
        loops: Int,
    ): LongArray

    /** What the long file weighs: every repeat copies [loopBytes] again, plus 2 % for the sample tables. */
    external fun loopEstimatedBytes(
        loopUs: Long,
        totalUs: Long,
        loopBytes: Long,
        soundtrackBytes: Long,
    ): Long

    /** The video bit rate that keeps a file [totalUs] long under [limitBytes]. */
    external fun loopFittingBitRate(
        totalUs: Long,
        limitBytes: Long,
        soundtrackBytes: Long,
    ): Int

    /** The progress bar while clip [index] of [clips] is [clipProgress] (0..1) transcoded. */
    external fun loopTranscodeProgress(
        index: Int,
        clipProgress: Float,
        clips: Int,
    ): Float

    /** Where a long-form copy stops writing: 4 GiB less 64 MiB for the index. */
    external fun exportMuxerHardLimitBytes(): Long

    /** The encoded frame for a short side and a ratio, into out[0] (width) and out[1] (height). */
    external fun exportFrameSize(
        shortSide: Int,
        wRatio: Int,
        hRatio: Int,
        out: IntArray,
    )

    /** What [renderEtaSample] answers while it has no estimate yet. */
    const val RENDER_ETA_UNKNOWN = -1L

    /** A render's estimate of the time left, for one run after another. Freed with [renderEtaDestroy]. */
    external fun renderEtaCreate(): Long

    external fun renderEtaDestroy(handle: Long)

    /** Feeds one progress sample; the seconds left, or [RENDER_ETA_UNKNOWN]. [steady] false while warming up. */
    external fun renderEtaSample(
        handle: Long,
        progress: Float,
        atMs: Long,
        steady: Boolean,
    ): Long

    external fun renderEtaReset(handle: Long)

    /** How [renderEtaPhrase] words the seconds left; the last two carry [renderEtaPhraseCount]. */
    const val ETA_ALMOST_DONE = 0
    const val ETA_ABOUT_A_MINUTE = 1
    const val ETA_MINUTES = 2
    const val ETA_HOURS = 3

    external fun renderEtaPhrase(secondsRemaining: Long): Int

    /** The minutes or hours the phrase names; 0 for the other two. */
    external fun renderEtaPhraseCount(secondsRemaining: Long): Int

    /** The maximum take lengths Settings › Export › Record offers, in seconds; 0 is "no limit". */
    external fun recordMaxLengths(): IntArray

    /** A stored maximum take length made one of [recordMaxLengths]. */
    external fun recordSnapMaxLength(requestedSec: Int): Int

    /** The bytes [lengthSec] of video at [videoBps] takes with its AAC, the muxer's target applied. */
    external fun recordEstimateBytes(
        videoBps: Int,
        lengthSec: Int,
    ): Long

    /** Whether a render that long lowers [videoBps] to stay under the muxer's target. */
    external fun recordRateLowered(
        videoBps: Int,
        lengthSec: Int,
    ): Boolean

    /** The one rate a mix's clips are brought to, so they share one AAC track (core/audio/dsp/PcmTiming). */
    external fun mixRateHz(): Int

    // Export post-passes (core/viz/frame/ExportPasses): the watermark and a loop's seam. [assets] is the
    // app's AssetManager (typed as Any, as for [VizNative.vizCreate]); every call but the watermark's pure calls
    // (placement to tile size) and the seam budget needs the export's GL context current, destroy included.

    /** "" when the pass built; otherwise why not (the handle must still be destroyed). */
    external fun exportPassError(handle: Long): String

    external fun exportPassDestroy(handle: Long)

    /**
     * Whether a render's frames carry the wordmark and where (`exportmath::watermark::placement`): 0 for
     * none, else the placement code (`dev.geode.model.WatermarkChoice`), an end card becoming a corner
     * mark on frames of a [use] whose end is not known.
     */
    external fun exportWatermarkPlacement(
        required: Boolean,
        proAdds: Boolean,
        placement: Int,
        use: Int,
    ): Int

    /**
     * The wordmark's paint for a frame whose short side is [shortSide], into [out] (5 floats): text size px,
     * alpha (0..255), letter spacing (em), shadow radius px, shadow alpha.
     */
    external fun exportWatermarkPaint(
        shortSide: Int,
        placement: Int,
        out: FloatArray,
    )

    /**
     * The tile for the text measured with that paint ([textWidth], the font's [ascent] and [descent] at [textPx])
     * into [out] (7 floats): width, height, rotation in degrees, its pivot x and y, where the text is drawn x and y.
     */
    external fun exportWatermarkRaster(
        textWidth: Float,
        ascent: Float,
        descent: Float,
        textPx: Float,
        placement: Int,
        out: FloatArray,
    )

    /**
     * [rgba]: a direct buffer of the tile's premultiplied RGBA8 pixels, top row first (Bitmap.copyPixelsToBuffer).
     * [lengthMs] is the video's, for an end card; 0 when unknown.
     */
    external fun exportWatermarkCreate(
        assets: Any,
        rgba: java.nio.ByteBuffer,
        tileWidth: Int,
        tileHeight: Int,
        placement: Int,
        corner: Int,
        lengthMs: Long,
    ): Long

    /** Over the default framebuffer, after the scene; [timeMs] is the frame's time in the video. */
    external fun exportWatermarkDraw(
        handle: Long,
        frameWidth: Int,
        frameHeight: Int,
        timeMs: Long,
    )
}
