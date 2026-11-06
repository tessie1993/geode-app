package dev.geode.export

import dev.geode.engine.bridge.GeodeNative

/**
 * Keeps a rendered MP4 inside what [android.media.MediaMuxer] can write.
 *
 * MediaMuxer records 32-bit chunk offsets, so a file that grows past four gigabytes is indexed
 * wrong and will not play. The quality presets fix the video bit rate (40 Mbit/s for 4K), and at
 * that rate a track longer than about twelve minutes crosses the line; 1080p crosses it at about
 * forty. Rather than fail an hour of GPU work at the end, the encoder is asked for a lower rate up
 * front: just low enough that video plus audio fit under the render target (3.8 GB).
 *
 * The arithmetic and both byte limits (the bit-rate target and [LoopExtend]'s hard stop) are native,
 * side by side in `core/output/EncodePlan` and tested by `core/tests/output_test.cpp`.
 */
object MuxerBudget {
    /** The AAC rate [AudioTranscoder] encodes at, for callers that have not transcoded yet. */
    const val AAC_BPS: Int = 192_000

    /**
     * [requestedBps], or less when [durationUs] of video at that rate plus [audioBytes] would pass
     * the 3.8 GB target. Never above [requestedBps] and never below 2 Mbit/s (unless the request
     * itself is lower). A duration that is not known yet (zero or less) leaves the request alone.
     */
    fun cappedVideoBps(
        requestedBps: Int,
        durationUs: Long,
        audioBytes: Long,
    ): Int = GeodeNative.exportCappedVideoBps(requestedBps, durationUs, audioBytes, limitBytes = 0L)

    /** What [durationUs] of audio at [bps] takes, for estimating before the audio exists. */
    fun audioBytes(
        durationUs: Long,
        bps: Int = AAC_BPS,
    ): Long = GeodeNative.exportAudioBytes(durationUs, bps)
}
