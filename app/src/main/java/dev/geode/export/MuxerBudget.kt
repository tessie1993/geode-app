package dev.geode.export

/**
 * Keeps a rendered MP4 inside what [android.media.MediaMuxer] can write.
 *
 * MediaMuxer records 32-bit chunk offsets, so a file that grows past four gigabytes is indexed
 * wrong and will not play (see [LoopExtend.MUXER_SAFE_BYTES]). The quality presets fix the video
 * bit rate (40 Mbit/s for 4K), and at that rate a track longer than about twelve minutes crosses
 * the line; 1080p crosses it at about forty. The render is not short of anything but bytes, so
 * rather than fail an hour of GPU work at the end, the encoder is asked for a lower rate up front:
 * just low enough that video plus audio fit under [SAFE_BYTES].
 *
 * Pure arithmetic so it can be tested off-device; the callers know the duration and the audio.
 */
object MuxerBudget {
    /**
     * The size a render aims under. Well below the 4 GiB ceiling (about 490 MB of headroom, 11%)
     * because a hardware encoder's bit rate is a target, not a promise: VBR encoders overshoot on
     * busy frames, and the container's own index and headers sit on top.
     */
    const val SAFE_BYTES: Long = 3_800_000_000L

    /**
     * The least the cap will go to. Below this a 4K frame is mush, and a track long enough to need
     * less (roughly four hours) is better refused by the muxer than rendered unwatchable.
     */
    const val FLOOR_BPS: Int = 2_000_000

    /** The AAC rate [AudioTranscoder] encodes at, for callers that have not transcoded yet. */
    const val AAC_BPS: Int = 192_000

    private const val US_PER_SECOND = 1_000_000L
    private const val BITS_PER_BYTE = 8L

    /**
     * [requestedBps], or less when [durationUs] of video at that rate plus [audioBytes] would pass
     * [limitBytes]. Never above [requestedBps] and never below [FLOOR_BPS] (unless the request
     * itself is lower). A duration that is not known yet (zero or less) leaves the request alone.
     */
    fun cappedVideoBps(
        requestedBps: Int,
        durationUs: Long,
        audioBytes: Long,
        limitBytes: Long = SAFE_BYTES,
    ): Int {
        if (durationUs <= 0L || requestedBps <= 0) return requestedBps
        val videoBytes = (limitBytes - audioBytes.coerceAtLeast(0L)).coerceAtLeast(0L)
        val fittingBps = videoBytes * BITS_PER_BYTE * US_PER_SECOND / durationUs
        if (fittingBps >= requestedBps) return requestedBps
        return fittingBps.toInt().coerceAtLeast(minOf(FLOOR_BPS, requestedBps))
    }

    /** What [durationUs] of audio at [bps] takes, for estimating before the audio exists. */
    fun audioBytes(
        durationUs: Long,
        bps: Int = AAC_BPS,
    ): Long = durationUs.coerceAtLeast(0L) * bps / (BITS_PER_BYTE * US_PER_SECOND)
}
