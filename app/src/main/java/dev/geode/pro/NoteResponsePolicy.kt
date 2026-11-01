package dev.geode.pro

/**
 * Whether Note & key reactions (core/viz/NoteResponse) run, from the user's switch and the entitlement.
 *
 * Pro and the trial get it; free does not, not even for a minute. Unlike the auto-director, whose taste
 * ([AutoDirectorPolicy]) shows a whole decision being made at the first section, a minute of note blooms
 * would only show what is being taken away when it ends. The switch stays as the user set it: a lapsed
 * trial turns the layer off in the engine (the renderer eases it out over about a second, so the end is a
 * fade), and buying Pro brings it back without asking again.
 */
object NoteResponsePolicy {
    fun active(
        enabled: Boolean,
        gate: FeatureGate,
    ): Boolean = enabled && gate.allows(ProFeature.NOTE_RESPONSE)

    /** Whether this entitlement locks the switch, so the screen shows the Pro badge and opens the paywall. */
    fun isLocked(gate: FeatureGate): Boolean = !gate.allows(ProFeature.NOTE_RESPONSE)
}
