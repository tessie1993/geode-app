package dev.geode.model

/**
 * A render's watermark as pinned when it starts: [required] by the entitlement, or [proAdds] (a Pro
 * user's own choice to mark their videos), at [placement] and [corner], on frames of [use]. The codes
 * are native's (`GEODE_WATERMARK_*` in `core/api/geode_api.h`); whether a frame is marked, where, and
 * what an end card becomes on frames with no known end is native too (`geode_export_watermark_placement`).
 */
data class WatermarkChoice(
    val required: Boolean,
    val proAdds: Boolean = false,
    val placement: Int = TILED,
    val corner: Int = BOTTOM_RIGHT,
    val use: Int = USE_WHOLE,
) {
    companion object {
        const val TILED = 1
        const val CORNER = 2
        const val END_CARD = 3
        const val TOP_LEFT = 0
        const val TOP_RIGHT = 1
        const val BOTTOM_LEFT = 2
        const val BOTTOM_RIGHT = 3
        const val USE_WHOLE = 0
        const val USE_LOOP = 2
        const val USE_LIVE = 3

        /** Settings › Preferences' choices, in the order its rows list them. */
        val PLACEMENTS = listOf(TILED, CORNER, END_CARD)
        val CORNERS = listOf(TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT)

        /** No mark: Pro's default. */
        val NONE = WatermarkChoice(required = false)
    }
}
