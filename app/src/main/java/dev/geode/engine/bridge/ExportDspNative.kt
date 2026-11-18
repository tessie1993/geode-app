package dev.geode.engine.bridge

// The JNI declarations of geode_export_dsp_jni.cpp (CLAUDE.md).
object ExportDspNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // Encoder fallback ladder (core/output/EncoderLadder): what an export asks the video encoder for.

    /**
     * Every encoder attempt for [requestedFps] at [bitRate], in order, as `[codecIndex, fps, bitRate]`
     * triples: the ladder of each candidate codec in turn. [ceilings] has one entry per codec (at most
     * two), its listed upper rate for the size, 0 when unknown.
     */
    external fun encoderAttempts(
        requestedFps: Int,
        bitRate: Int,
        ceilings: IntArray,
    ): IntArray
}
