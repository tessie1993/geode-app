package dev.geode.nav.connect

/** What the person asked of motion. The design system scales every animation by this. */
data class MotionPolicy(
    /** Cut all decorative movement: no ripples, ink, drift or stretch; transitions become cuts or fades. */
    val reducedMotion: Boolean = false,
    /** 0–1 scale on whatever liquid effects remain; 0 pauses any simulation outright. */
    val intensity: Float = 1f,
)
