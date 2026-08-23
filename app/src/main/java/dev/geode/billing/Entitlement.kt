package dev.geode.billing

/**
 * What the person is entitled to.
 *
 * Two states and no more. Anything finer — trials, tiers, per-feature unlocks — multiplies the
 * places a limit has to be checked, and a limit that is checked in five places is a limit that is
 * wrong in one of them.
 */
enum class Tier {
    FREE,
    UNLOCKED,
    ;

    val limits: ExportLimits
        get() =
            when (this) {
                FREE -> ExportLimits.FREE
                UNLOCKED -> ExportLimits.UNLOCKED
            }

    /** Ads exist only to be removed by unlocking; an unlocked app never shows one. */
    val showsAds: Boolean get() = this == FREE
}

/**
 * The ceiling on what a tier may render.
 *
 * Expressed as data rather than as `if (tier == FREE)` scattered through the export path, so there
 * is exactly one place to read when someone asks "what does free actually get?".
 */
data class ExportLimits(
    val maxDurationMs: Long,
    val maxShortSide: Int,
    val watermark: Boolean,
) {
    companion object {
        /**
         * One minute at 720p, with a corner mark.
         *
         * The mark is a corner logo and never crosses the visual — a watermark stamped over the
         * middle of the picture makes the free tier useless as a preview of the paid one, which
         * defeats the point of having a free tier at all.
         */
        val FREE =
            ExportLimits(
                maxDurationMs = 60_000L,
                maxShortSide = 720,
                watermark = true,
            )

        val UNLOCKED =
            ExportLimits(
                maxDurationMs = Long.MAX_VALUE,
                maxShortSide = Int.MAX_VALUE,
                watermark = false,
            )
    }
}

/**
 * The answer to "may this export run?".
 *
 * A refusal carries the reason and the nearest thing that *would* work, because a limit the user
 * cannot act on reads as a bug. This is the honest-empty-state rule applied to money: say what
 * happened and what to do instead.
 */
sealed interface ExportVerdict {
    data object Allowed : ExportVerdict

    data class Blocked(
        val reason: BlockedReason,
        /** The same request, trimmed to what this tier can actually do. */
        val nearestAllowed: ExportRequest,
    ) : ExportVerdict
}

sealed interface BlockedReason {
    data class TooLong(
        val requestedMs: Long,
        val limitMs: Long,
    ) : BlockedReason

    data class TooLarge(
        val requestedShortSide: Int,
        val limitShortSide: Int,
    ) : BlockedReason

    /** Both ceilings were exceeded; reported together so the user is not refused twice. */
    data class TooLongAndTooLarge(
        val requestedMs: Long,
        val limitMs: Long,
        val requestedShortSide: Int,
        val limitShortSide: Int,
    ) : BlockedReason
}

/** The shape of an export, reduced to the two dimensions a tier can constrain. */
data class ExportRequest(
    val durationMs: Long,
    val shortSide: Int,
)

object ExportGate {
    /**
     * Checks [request] against [tier].
     *
     * Deliberately pure: no context, no billing client, no side effects. The gate is a function of
     * the tier and the request, which means the same call can drive the export path, the greyed-out
     * state of the size picker, and the upgrade prompt without any of them drifting apart.
     */
    fun check(
        tier: Tier,
        request: ExportRequest,
    ): ExportVerdict {
        val limits = tier.limits
        val tooLong = request.durationMs > limits.maxDurationMs
        val tooLarge = request.shortSide > limits.maxShortSide
        if (!tooLong && !tooLarge) return ExportVerdict.Allowed

        val nearest =
            ExportRequest(
                durationMs = minOf(request.durationMs, limits.maxDurationMs),
                shortSide = minOf(request.shortSide, limits.maxShortSide),
            )
        val reason =
            when {
                tooLong && tooLarge ->
                    BlockedReason.TooLongAndTooLarge(
                        requestedMs = request.durationMs,
                        limitMs = limits.maxDurationMs,
                        requestedShortSide = request.shortSide,
                        limitShortSide = limits.maxShortSide,
                    )
                tooLong -> BlockedReason.TooLong(request.durationMs, limits.maxDurationMs)
                else -> BlockedReason.TooLarge(request.shortSide, limits.maxShortSide)
            }
        return ExportVerdict.Blocked(reason, nearest)
    }
}
