package dev.geode.engine.bridge

// The JNI declarations of geode_responder_jni.cpp and the renderer's two Event Responder calls in geode_viz_jni.cpp
// (CLAUDE.md): declarations only.
object ResponderNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // The Event Responder (core/viz/modulation/Responder, api/geode_responder_api.h): Customize > Music's Song event rows,
    // each owning an ordered list of responses. The responses ride in the same saved text as the routes; native reads,
    // edits, lists and plays them.

    /** The saved text of a new style: every row off. */
    external fun responderDefaultText(): String?

    /** A row's saved name (KICK … KEY_CHANGE, COLLIDE), or null for a row there is none of. */
    external fun responderRowName(row: Int): String?

    /**
     * Every row's [RESP_ROW_VALUES] (on, the user's responses, the style's default responses, how many of those are
     * muted), in row order, for the saved text (null: a new style's) on [styleId]. A row's switch is for its
     * responses alone: the style's own routes play whatever the rows say.
     */
    external fun responderRows(
        reactions: String?,
        styleId: String,
        seed: Int,
        mask: Int,
    ): FloatArray?

    /** One row's responses, [RESP_LIST_VALUES] each: the user's in order, then the style's own routes on its events. */
    external fun responderValues(
        reactions: String?,
        styleId: String,
        seed: Int,
        mask: Int,
        row: Int,
    ): FloatArray?

    /** One edit ([RESP_EDIT_ROW_ON] and the rest): the new reactions text, routes and all; null when native could not read or make it. */
    external fun responderEdit(
        reactions: String?,
        op: Int,
        row: Int,
        index: Int,
        field: Int,
        value: Float,
    ): String?

    /** The Song events page's catalogue (core/viz/modulation/ResponseCatalog): its lines are listed in ResponseCatalog.hpp. */
    external fun responderCatalogText(): String

    /**
     * One catalogue edit ([RESP_CATALOG_ADD] and the rest) on the saved text (null: a new style's): the new reactions
     * text. [value] is the tile or preset ordinal.
     */
    external fun responderCatalogEdit(
        reactions: String?,
        op: Int,
        row: Int,
        index: Int,
        value: Float,
    ): String

    /** A response's [field] ([RESP_AMOUNT] and the rest) as words; [look] is the params frame an amount is a percentage of. */
    external fun responderFieldText(
        values: FloatArray,
        field: Int,
        look: FloatArray?,
    ): String

    /** What a response's chip says. [values] are its [RESP_FIELDS] fields (a listed response's first ones). */
    external fun responderTitle(values: FloatArray): String

    /** The envelope preset (a catalogue `preset` line's ordinal) a response's envelope is, or -1. */
    external fun responderPresetOf(values: FloatArray): Int

    /** Plays a response once now on a renderer, as its event would (the picker's preview): its [RESP_FIELDS] fields. Any thread. */
    external fun responderPreview(
        handle: Long,
        values: FloatArray,
    )

    /** A renderer's rows' event lights, then its cost budget ([RESP_STATUS_COST] and the rest), as the last frame left them. */
    external fun responderStatus(handle: Long): FloatArray?

    // Mirrors of GEODE_RESP_*.
    const val RESP_ROWS: Int = 16
    const val RESP_FIELDS: Int = 20
    const val RESP_LIST_VALUES: Int = 24
    const val RESP_ROW_VALUES: Int = 4
    const val RESP_ORIGIN_USER: Int = 0
    const val RESP_ORIGIN_STYLE: Int = 1

    // A response's fields.
    const val RESP_ENABLED: Int = 0
    const val RESP_ROW: Int = 1
    const val RESP_KIND: Int = 2
    const val RESP_TARGET: Int = 3
    const val RESP_AMOUNT: Int = 4
    const val RESP_STRENGTH: Int = 5
    const val RESP_FADE_IN: Int = 6
    const val RESP_HOLD: Int = 7
    const val RESP_FADE_OUT: Int = 8
    const val RESP_UNIT: Int = 9
    const val RESP_DRIVE: Int = 10
    const val RESP_EVERY_NTH: Int = 11
    const val RESP_CHANCE: Int = 12
    const val RESP_COOLDOWN: Int = 13
    const val RESP_P0: Int = 14

    // The catalogue edit ops.
    const val RESP_CATALOG_ADD: Int = 0
    const val RESP_CATALOG_PRESET: Int = 1
    const val RESP_CATALOG_RESET_ROW: Int = 2

    // The edit ops.
    const val RESP_EDIT_ROW_ON: Int = 0
    const val RESP_EDIT_ADD: Int = 1
    const val RESP_EDIT_REMOVE: Int = 2
    const val RESP_EDIT_SET: Int = 3
    const val RESP_EDIT_CLEAR: Int = 4
    const val RESP_EDIT_STYLE_MUTE: Int = 5

    // The status values after the rows' lights.
    const val RESP_STATUS_COST: Int = 16
    const val RESP_STATUS_BUDGET: Int = 17
    const val RESP_STATUS_LEVEL: Int = 18
    const val RESP_STATUS_ACTIVE: Int = 19
    const val RESP_STATUS_DROPPED: Int = 20
    const val RESP_STATUS_ROUTES: Int = 21
    const val RESP_STATUS_VALUES: Int = 22
}

/**
 * How a style's own reaction on a Song event row is switched off and on (its chip's own switch). A row that is off
 * never mutes the style's reactions, and there is no master switch: each chip has its own.
 *
 * TODO(feat/event-responder #1414): the native call that lists a row's style routes and sets or clears a chip's off
 * switch, kept in the reactions text, is not on this base. Until it lands the switch is the route's own enabled field,
 * through the route edit the reactions editor already uses; swap the implementation for that call and keep this seam.
 */
fun interface StyleChipSwitch {
    fun set(
        routeIndex: Int,
        on: Boolean,
    )
}
