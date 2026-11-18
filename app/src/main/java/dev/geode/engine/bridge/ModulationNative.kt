package dev.geode.engine.bridge

// The JNI declarations of geode_modulation_jni.cpp (CLAUDE.md).
object ModulationNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // The user's modulation (core/viz/ModDocument, api/geode_modulation_api.h): the waves, the envelopes and
    // the reaction routes. The shell holds each as the text it saves; native reads, edits and plays them.

    /** What the editors show (geode_mod_catalog_text): every name, label, range and count, as a line table. */
    external fun modCatalogText(): String

    /** The saved waves as native reads them: every slot and field, each in range. Null or broken text: the defaults. */
    external fun modLfosRead(text: String?): String

    /** The saved envelopes, likewise. */
    external fun modAdsrsRead(text: String?): String

    /** The user's saved routes as native reads them; null for text that is no route list (Auto). */
    external fun modReactionsRead(text: String?): String?

    /** One wave slot: its [MOD_LFO_ENABLED]..[MOD_LFO_MOMENT] fields, then 1 while its target moves [styleId]. */
    external fun modLfoValues(
        lfos: String,
        slot: Int,
        styleId: String,
    ): FloatArray

    /** [lfos] with one [field] of one [slot] set (geode_mod_lfo_edit); enums go as their catalog ordinal. */
    external fun modLfoEdit(
        lfos: String,
        slot: Int,
        field: Int,
        value: Float,
    ): String

    /** One envelope: its [MOD_ADSR_ENABLED]..[MOD_ADSR_MOMENT] fields, then the ordinals of the targets it moves. */
    external fun modAdsrValues(
        adsrs: String,
        slot: Int,
    ): FloatArray

    /** [adsrs] with one [field] of one [slot] set; [MOD_ADSR_TARGET] adds or takes away the target [value] names. */
    external fun modAdsrEdit(
        adsrs: String,
        slot: Int,
        field: Int,
        value: Float,
    ): String

    /** Which of the wave [slot]'s controls do anything as it stands (geode_mod_lfo_shows): [MOD_SHOWS_WAVE] and the rest. */
    external fun modLfoShows(
        lfos: String,
        slot: Int,
    ): Int

    /** Which of the envelope [slot]'s controls do anything as it stands (geode_mod_adsr_shows). */
    external fun modAdsrShows(
        adsrs: String,
        slot: Int,
    ): Int

    // Mirrors of GEODE_MOD_SHOWS_*: the bits [modLfoShows] and [modAdsrShows] return.
    const val MOD_SHOWS_WAVE: Int = 1
    const val MOD_SHOWS_RATE: Int = 2
    const val MOD_SHOWS_KNOB: Int = 4

    /** The target ordinals a [picker] (a wave slot, or [MOD_PICKER_REACTION]) lists on [styleId]. */
    external fun modTargetsOffered(
        styleId: String,
        picker: Int,
    ): IntArray

    /** The target ordinals an envelope's picker lists on [styleId]. */
    external fun modAdsrTargetsOffered(
        adsrs: String,
        slot: Int,
        styleId: String,
    ): IntArray

    /**
     * What plays on [styleId] (geode_mod_routes_playing): the user's [reactions] (null: Auto) on the hand
     * [mask]'s sources, the style's own on the rest. [MOD_ROUTE_VALUES] per route.
     */
    external fun modRoutesPlaying(
        styleId: String,
        seed: Int,
        reactions: String?,
        mask: Int,
    ): FloatArray

    /**
     * One edit of the reactions (geode_mod_reactions_edit, [op] a `MOD_EDIT_*`). Returns the user's new routes
     * and writes the mask to save beside them to `outMask[0]`; null (and mask 0) when every route is Auto again.
     */
    external fun modReactionsEdit(
        styleId: String,
        seed: Int,
        reactions: String?,
        mask: Int,
        op: Int,
        a: Int,
        b: Int,
        value: Float,
        outMask: IntArray,
    ): String?

    /** Customize › Music's rows, [MOD_MUSIC_VALUES] per moment: mine, route count, strength, on bits, fits bits. */
    external fun modMusicState(
        styleId: String,
        seed: Int,
        reactions: String?,
        mask: Int,
    ): FloatArray

    /** [MOD_RECIPE_ON], [MOD_RECIPE_FITS] and [MOD_RECIPE_OFFERED] for [recipe] on [styleId] over what plays. */
    external fun modRecipeState(
        recipe: Int,
        styleId: String,
        seed: Int,
        reactions: String?,
        mask: Int,
    ): Int

    // Mirrors of GEODE_MOD_LFO_*: a wave's fields.
    const val MOD_LFO_ENABLED: Int = 0
    const val MOD_LFO_SOURCE: Int = 1
    const val MOD_LFO_TARGET: Int = 2
    const val MOD_LFO_WAVE: Int = 3
    const val MOD_LFO_RATE_SECONDS: Int = 4
    const val MOD_LFO_DEPTH: Int = 5
    const val MOD_LFO_POLARITY: Int = 6
    const val MOD_LFO_CURVE: Int = 7
    const val MOD_LFO_BEATS: Int = 8
    const val MOD_LFO_KNOB: Int = 9
    const val MOD_LFO_MOMENT: Int = 10

    /** Where [modLfoValues] says whether the slot's target moves the style. */
    const val MOD_LFO_REACHES: Int = 11

    // Mirrors of GEODE_MOD_ADSR_*: an envelope's fields; TARGET is edit-only.
    const val MOD_ADSR_ENABLED: Int = 0
    const val MOD_ADSR_ATTACK: Int = 1
    const val MOD_ADSR_DECAY: Int = 2
    const val MOD_ADSR_SUSTAIN: Int = 3
    const val MOD_ADSR_RELEASE: Int = 4
    const val MOD_ADSR_AMOUNT: Int = 5
    const val MOD_ADSR_BAND: Int = 6
    const val MOD_ADSR_GATE_THRESHOLD: Int = 7
    const val MOD_ADSR_SUSTAIN_TRACK: Int = 8
    const val MOD_ADSR_RETRIGGER: Int = 9
    const val MOD_ADSR_KNOB: Int = 10
    const val MOD_ADSR_MOMENT: Int = 11
    const val MOD_ADSR_TARGET: Int = 12

    /** Mirrors GEODE_MOD_ADSR_VALUES: the fields [modAdsrValues] writes before the target ordinals. */
    const val MOD_ADSR_VALUES: Int = 12

    // Mirrors of GEODE_MOD_ROUTE_*: a route's fields.
    const val MOD_ROUTE_ENABLED: Int = 0
    const val MOD_ROUTE_SOURCE: Int = 1
    const val MOD_ROUTE_TARGET: Int = 2
    const val MOD_ROUTE_MODE: Int = 3
    const val MOD_ROUTE_AMOUNT: Int = 4
    const val MOD_ROUTE_CURVE: Int = 5
    const val MOD_ROUTE_ATTACK_SECONDS: Int = 6
    const val MOD_ROUTE_RELEASE_SECONDS: Int = 7

    /** Where a [modRoutesPlaying] route says whether its target moves the style. */
    const val MOD_ROUTE_REACHES: Int = 8

    /** Mirrors GEODE_MOD_ROUTE_VALUES. */
    const val MOD_ROUTE_VALUES: Int = 9

    // Mirrors of GEODE_MOD_EDIT_*: [modReactionsEdit]'s op.
    const val MOD_EDIT_ROUTE_SET: Int = 0
    const val MOD_EDIT_ROUTE_ADD: Int = 1
    const val MOD_EDIT_ROUTE_REMOVE: Int = 2
    const val MOD_EDIT_RECIPE_TOGGLE: Int = 3
    const val MOD_EDIT_MUSIC_TOGGLE: Int = 4
    const val MOD_EDIT_MUSIC_STRENGTH: Int = 5
    const val MOD_EDIT_MUSIC_RESET: Int = 6
    const val MOD_EDIT_CLEAR: Int = 7

    /** Mirrors GEODE_MOD_RECIPE_ON: every route the recipe would add to the style already plays. */
    const val MOD_RECIPE_ON: Int = 1

    /** Mirrors GEODE_MOD_RECIPE_FITS: turning the recipe on stays within the matrix. */
    const val MOD_RECIPE_FITS: Int = 2

    /** Mirrors GEODE_MOD_RECIPE_OFFERED: the recipe has a route the style can show. */
    const val MOD_RECIPE_OFFERED: Int = 4

    /** Mirrors GEODE_MOD_MUSIC_VALUES. */
    const val MOD_MUSIC_VALUES: Int = 5

    /** Mirrors GEODE_MOD_PICKER_REACTION: the route editor's target picker. */
    const val MOD_PICKER_REACTION: Int = -2

    /**
     * The hand mask for the saved routes [reactions] (null: none saved): 0 with none, [GeodeNative.HAND_ALL] when no mask was saved
     * beside them ([hasMask] false), else [savedMask]; -1 when the text is no route list any more (geode_mod_hand_load).
     */
    external fun modHandLoad(
        reactions: String?,
        hasMask: Boolean,
        savedMask: Int,
    ): Int
}
