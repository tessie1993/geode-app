package dev.geode.render.fluid

/**
 * The fluid simulation's quality tiers.
 *
 * Only [LABELS] is public: the UI offers the tiers as a chip row and stores the chosen index,
 * but the resolutions and iteration counts behind each tier are the simulation's business.
 */
object FluidQuality {
    internal data class Tier(
        val label: String,
        val simRes: Int,
        val dyeRes: Int,
        val particleSide: Int,
        val iterations: Int,
    )

    internal val TIERS: List<Tier> =
        listOf(
            Tier("Ultra", 256, 1024, 1024, 28),
            Tier("High", 192, 768, 768, 24),
            Tier("Medium", 128, 512, 512, 20),
            Tier("Low", 96, 384, 320, 16),
            Tier("Min", 64, 256, 160, 12),
        )

    /** Tier names, finest first; a stored quality setting is an index into this list. */
    val LABELS: List<String> = TIERS.map { it.label }

    internal fun tier(index: Int): Tier = TIERS[index.coerceIn(0, TIERS.size - 1)]

    internal fun effectiveIndex(
        userIndex: Int,
        autoDowngradeSteps: Int,
    ): Int = (userIndex.coerceIn(0, TIERS.size - 1) + autoDowngradeSteps.coerceAtLeast(0)).coerceAtMost(TIERS.size - 1)
}
