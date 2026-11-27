package dev.geode.engine.bridge

// The JNI declarations of geode_panel_jni.cpp.
object PanelNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /**
     * Randomize for the waves native holds (core/viz/WaveRandomizer, geode_mod_state_waves_roll), seeded natively, with the
     * keep set it holds, on the look [params]; [all] is the wave editor's button. An event: nothing comes back.
     */
    external fun modStateWavesRoll(
        styleId: String,
        all: Boolean,
        params: FloatArray,
    )

    /**
     * Customize's dice (core/viz/PanelRoll), seeded natively: [category] (a CustomizeMap name) or everything when null,
     * from [input] into [output], holding [kept] (the user's lock set, one a line) and with [basicsOnly] every control
     * below a Basics. Returns [PANEL_ROLL_CHANGED] and [PANEL_ROLL_WAVES] bits.
     */
    external fun panelRoll(
        styleId: String,
        category: String?,
        kept: String,
        basicsOnly: Boolean,
        input: FloatArray,
        output: FloatArray,
    ): Int

    /** Randomize everything's roll (core/viz/RandomizeAll) with every category rolled, [kept] and [basicsOnly] as [panelRoll]; the same bits. */
    external fun panelRollAll(
        styleId: String,
        kept: String,
        basicsOnly: Boolean,
        seed: Long,
        before: FloatArray,
        base: FloatArray,
        output: FloatArray,
    ): Int

    const val PANEL_ROLL_CHANGED: Int = 1
    const val PANEL_ROLL_WAVES: Int = 2

    /** Music's new personality: [input] with the reaction DNA's seed one further, into [output]. */
    external fun paramsRerollReactions(
        input: FloatArray,
        output: FloatArray,
    )

    /**
     * Customize's undo (core/viz/ParamHistory): the look [before] (its frame, saved palette id and the [style] it was made on) is about to be
     * replaced at [nowMs]; edits close together fold into one entry, a [discrete] one never does. Returns
     * [PARAM_HISTORY_UNDO] and [PARAM_HISTORY_REDO] bits.
     */
    external fun paramHistoryRecord(
        before: FloatArray,
        palette: String,
        style: String,
        nowMs: Long,
        discrete: Boolean,
    ): Int

    /** The look to put up instead of [current] on [style] ([redo]: the last undone one; entries from another style are dropped): its frame into [output], its palette id back; null for none. */
    external fun paramHistoryStep(
        redo: Boolean,
        current: FloatArray,
        palette: String,
        style: String,
        output: FloatArray,
    ): String?

    external fun paramHistoryState(): Int

    const val PARAM_HISTORY_UNDO: Int = 1
    const val PARAM_HISTORY_REDO: Int = 2
}
