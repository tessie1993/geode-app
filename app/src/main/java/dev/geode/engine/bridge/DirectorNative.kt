package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for the director and its setters.
object DirectorNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /**
     * The auto-director (core/viz/Director): the music's feeling decides behaviour (axes, camera, motion)
     * over every style; never a colour, never a route. [on] is the user's switch as set, which an export directs by (pinned natively);
     * [amount] 0 off .. 1 full is what live surfaces drive with now; [seed] is the play's, so a new seed
     * starts the song afresh and an export pinned with it makes the same film.
     */
    external fun directorSet(
        on: Boolean,
        amount: Float,
        seed: Long,
    )

    /**
     * What live surfaces drive with (core/viz/Director, director::liveAmount), 0 or 1: from the switch [on], whether
     * the entitlement keeps the whole song ([entitled]: Pro or the trial), the song's [positionMs] and whether the
     * app's own player is [playing]. Free gets the first minute while it plays. Hand the result to [directorSet].
     */
    external fun directorLiveAmount(
        on: Boolean,
        entitled: Boolean,
        positionMs: Long,
        playing: Boolean,
    ): Float

    /**
     * How far the director reaches past its look (core/viz/DirectorReach): 0 the look alone, 1 it also
     * shuffles Customize categories at a new section or a drop, 2 it also runs slow modulators of its own.
     * Process-wide like [directorSet] and pinned with it by an export; nothing happens while Auto is off.
     */
    external fun directorReachSet(level: Int)

    /**
     * The Auto Director's settings (core/viz/DirectorTuning), process-wide like [directorSet] and pinned with
     * it by an export: [text] as [directorTuningWith] gave it ("" is every default).
     */
    external fun directorTuningSet(text: String)

    /** How many rows the Auto Director's settings table has. */
    external fun directorTuningCount(): Int

    /** Row [index]'s label, as native names it. */
    external fun directorTuningLabel(index: Int): String

    /** Row [index]'s one line of help. */
    external fun directorTuningHelp(index: Int): String

    /** Row [index]'s min, max, step (0 continuous) and 1 when it is a switch (on/off), else 0. */
    external fun directorTuningRange(index: Int): FloatArray

    /** Row [index]'s value in the saved [text]. */
    external fun directorTuningValue(
        text: String,
        index: Int,
    ): Float

    /** Row [index]'s value in the saved [text], as the page shows it. */
    external fun directorTuningValueText(
        text: String,
        index: Int,
    ): String

    /** The saved [text] with row [index] set to [value], held to its range: what the store saves. */
    external fun directorTuningWith(
        text: String,
        index: Int,
        value: Float,
    ): String

    /** Process-wide (core/viz/ShaderOverrides): every renderer draws it; "" clears it. */
    external fun shaderOverrideSet(
        styleId: String,
        fragmentSource: String,
    )

    /** The style's shader override; null when it draws its built-in shader. */
    external fun shaderOverride(styleId: String): String?

    /**
     * Customize › Music's hand mask (core/viz Reactions.hpp `hand::`): bit i is moment i set by hand,
     * [HAND_UNCLAIMED] every source no moment claims, [GeodeNative.HAND_ALL] a whole list of the user's. Process-wide like
     * [directorSet]: every renderer's director keeps its hands off those moments. Without a list of the user's
     * ([hasUser] false) native counts the mask as 0, so a stale mask cannot switch off the director's routes.
     */
    external fun handSet(
        mask: Int,
        hasUser: Boolean,
    )

    /** Bit per Music action whose target reaches [styleId] (geode_music_actions_for). */
    external fun musicActionsFor(styleId: String): Int

    /** Mirrors GEODE_HAND_UNCLAIMED. */
    const val HAND_UNCLAIMED: Int = 0x40000000
}
