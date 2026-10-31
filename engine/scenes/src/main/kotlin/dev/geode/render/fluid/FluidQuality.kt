package dev.geode.render.fluid

/**
 * The names of the fluid simulation's quality tiers.
 *
 * The UI offers the tiers as a chip row and stores the chosen index. The resolutions and solver
 * iterations behind each tier, and the auto-downgrade, are native (core/viz/fluid/FluidQuality);
 * the catalogue's `fluidTiers` value says how many there are, and a JVM test holds these labels to it.
 */
object FluidQuality {
    /** Tier names, finest first; a stored quality setting is an index into this list. */
    val LABELS: List<String> = listOf("Ultra", "High", "Medium", "Low", "Min")
}
