package dev.geode.pro

/**
 * Everything the one-time Pro unlock opens up.
 *
 * Pure Kotlin on purpose (no `android.*`): this, [FreeTier], [FeatureGate] and the use case are
 * the part of billing that would move unchanged into a shared module, while the Play client stays
 * behind [BillingRepository] in the Android layer.
 *
 * Which of these a free user still gets is not decided here but in [FreeTier], so the shape of the
 * free tier can be tuned without touching a single gate.
 */
enum class ProFeature {
    /** Exports with a short side above 1080 px — the 4K "Master" preset and its relatives. */
    EXPORT_4K,

    /** Exports above [FreeTier.FREE_MAX_FPS]. */
    EXPORT_60FPS,

    /** Exports without the full-frame wordmark. */
    NO_WATERMARK,

    /** Exports longer than [FreeTier.FREE_MAX_EXPORT_MS], loop renders, and Studio exports. */
    FULL_LENGTH_EXPORT,

    /** The multi-clip Studio editor: timeline, keyframes, transitions, auto-cut, captions. */
    STUDIO,

    /** The visualizer as a live wallpaper. */
    LIVE_WALLPAPER,

    /** The visualizer as a screensaver (Daydream). */
    SCREENSAVER,

    /** Every visual style outside [FreeTier.freeStyleIds]. */
    PREMIUM_STYLES,

    /**
     * Deeper look controls beyond the free basics: today the macro editor and the macro-preset
     * folder (the six standard knobs stay free). Further gates the free tier is still being tuned
     * around (see [FreeTier]) can join it later.
     */
    ADVANCED_CUSTOMIZATION,
}
