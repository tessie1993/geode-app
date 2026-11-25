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
    external fun responderDefaultText(): String

    /** A row's saved name (KICK … KEY_CHANGE, COLLIDE), or null for a row there is none of. */
    external fun responderRowName(row: Int): String?

    /**
     * Every row's [RESP_ROW_VALUES] (on, the user's responses, the style's default responses, whether it gates the
     * style's routes), in row order, for the saved text (null: a new style's) on [styleId].
     */
    external fun responderRows(
        reactions: String?,
        styleId: String,
        seed: Int,
        mask: Int,
    ): FloatArray

    /** One row's responses, [RESP_LIST_VALUES] each: the user's in order, then the style's own routes on its events. */
    external fun responderValues(
        reactions: String?,
        styleId: String,
        seed: Int,
        mask: Int,
        row: Int,
    ): FloatArray

    /** One edit ([RESP_EDIT_ROW_ON] and the rest): the new reactions text, routes and all. */
    external fun responderEdit(
        reactions: String?,
        op: Int,
        row: Int,
        index: Int,
        field: Int,
        value: Float,
    ): String

    /** Plays a response once now on a renderer, as its event would (the picker's preview): its [RESP_FIELDS] fields. Any thread. */
    external fun responderPreview(
        handle: Long,
        values: FloatArray,
    )

    /** A renderer's rows' event lights, then its cost budget ([RESP_STATUS_COST] and the rest), as the last frame left them. */
    external fun responderStatus(handle: Long): FloatArray

    // Mirrors of GEODE_RESP_*.
    const val RESP_ROWS: Int = 16
    const val RESP_FIELDS: Int = 20
    const val RESP_LIST_VALUES: Int = 22
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

    // The edit ops.
    const val RESP_EDIT_ROW_ON: Int = 0
    const val RESP_EDIT_ADD: Int = 1
    const val RESP_EDIT_REMOVE: Int = 2
    const val RESP_EDIT_SET: Int = 3
    const val RESP_EDIT_CLEAR: Int = 4

    // The status values after the rows' lights.
    const val RESP_STATUS_COST: Int = 16
    const val RESP_STATUS_BUDGET: Int = 17
    const val RESP_STATUS_LEVEL: Int = 18
    const val RESP_STATUS_ACTIVE: Int = 19
    const val RESP_STATUS_DROPPED: Int = 20
    const val RESP_STATUS_ROUTES: Int = 21
    const val RESP_STATUS_VALUES: Int = 22
}
