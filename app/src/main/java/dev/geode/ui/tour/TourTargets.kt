package dev.geode.ui.tour

import androidx.annotation.StringRes
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.node.CompositionLocalConsumerModifierNode
import androidx.compose.ui.node.GlobalPositionAwareModifierNode
import androidx.compose.ui.node.ModifierNodeElement
import androidx.compose.ui.node.currentValueOf
import androidx.compose.ui.platform.LocalView
import dev.geode.R
import dev.geode.model.TutorialChapter

/**
 * A control the tour can point at, with what the pointer says about it.
 *
 * Each spot belongs to one screen's set of pointers ([chapter]), in the order they play. A screen
 * marks its real controls with [tourTarget]; the tour never draws a copy of a control, it lights
 * the one on screen. A spot no screen has marked yet still plays on a replay, as a card with no
 * spotlight, so the words are never lost while a screen is being rebuilt.
 */
enum class TourSpot(
    val chapter: TutorialChapter,
    @param:StringRes val titleRes: Int,
    @param:StringRes val bodyRes: Int,
) {
    NAV_LIBRARY(TutorialChapter.WELCOME, R.string.tour_guide_library_title, R.string.tour_guide_library_body),
    NAV_VISUALS(TutorialChapter.WELCOME, R.string.tour_guide_visuals_title, R.string.tour_guide_visuals_body),

    PLAYER_PLAY(TutorialChapter.PLAYER, R.string.tour_player_play_title, R.string.tour_player_play_body),
    PLAYER_SEEK(TutorialChapter.PLAYER, R.string.tour_player_seek_title, R.string.tour_player_seek_body),
    PLAYER_QUEUE(TutorialChapter.PLAYER, R.string.tour_player_queue_title, R.string.tour_player_queue_body),

    LIBRARY_SEARCH(TutorialChapter.LIBRARY, R.string.tour_library_search_title, R.string.tour_library_search_body),
    LIBRARY_TRACK(TutorialChapter.LIBRARY, R.string.tour_library_track_title, R.string.tour_library_track_body),
    MINI_PLAYER(TutorialChapter.LIBRARY, R.string.tour_mini_player_title, R.string.tour_mini_player_body),

    VISUALS_SHEET(TutorialChapter.VISUALS, R.string.tour_visuals_sheet_title, R.string.tour_visuals_sheet_body),
    VISUALS_TABS(TutorialChapter.VISUALS, R.string.tour_visuals_tabs_title, R.string.tour_visuals_tabs_body),
    STAGE_ARROWS(TutorialChapter.VISUALS, R.string.tour_stage_arrows_title, R.string.tour_stage_arrows_body),
    STAGE_RECORD(TutorialChapter.VISUALS, R.string.tour_stage_record_title, R.string.tour_stage_record_body),

    CUSTOMIZE_PAGES(TutorialChapter.CUSTOMIZE, R.string.tour_customize_pages_title, R.string.tour_customize_pages_body),
    CUSTOMIZE_TOOLS(TutorialChapter.CUSTOMIZE, R.string.tour_customize_tools_title, R.string.tour_customize_tools_body),
    CUSTOMIZE_ROW_MENU(TutorialChapter.CUSTOMIZE, R.string.tour_customize_row_title, R.string.tour_customize_row_body),

    STUDIO_TABS(TutorialChapter.STUDIO, R.string.tour_studio_tabs_title, R.string.tour_studio_tabs_body),
    STUDIO_RENDER(TutorialChapter.STUDIO, R.string.tour_studio_render_title, R.string.tour_studio_render_body),
    STUDIO_MY_VIDEOS(TutorialChapter.STUDIO, R.string.tour_studio_videos_title, R.string.tour_studio_videos_body),

    SETTINGS_GROUPS(TutorialChapter.SETTINGS, R.string.tour_settings_groups_title, R.string.tour_settings_groups_body),
    SETTINGS_PLAYBACK(TutorialChapter.SETTINGS, R.string.tour_settings_playback_title, R.string.tour_settings_playback_body),
    SETTINGS_TUTORIALS(TutorialChapter.SETTINGS, R.string.tour_settings_tutorials_title, R.string.tour_settings_tutorials_body),
}

/** The chapters the tour plays, in order. The model's other chapters were carousel-only. */
val TourChapters: List<TutorialChapter> =
    listOf(
        TutorialChapter.WELCOME,
        TutorialChapter.PLAYER,
        TutorialChapter.LIBRARY,
        TutorialChapter.VISUALS,
        TutorialChapter.CUSTOMIZE,
        TutorialChapter.STUDIO,
        TutorialChapter.SETTINGS,
    )

/** A chapter's pointers, in the order they play. */
val TutorialChapter.spots: List<TourSpot> get() = TourSpot.entries.filter { it.chapter == this }

/**
 * Where each marked control is on screen right now, in screen pixels.
 *
 * Process-wide, like the one renderer: the tour draws in a dialog window of its own, so it
 * cannot read the app's layout through composition, and screen coordinates are the one frame
 * both windows share (split screen and freeform windows included). Snapshot state, so the
 * spotlight follows a control that moves (a sheet settling, a list scrolling).
 *
 * Each entry remembers which node wrote it: two nodes can briefly carry one spot while a screen
 * swaps its content, and the one leaving must not erase the one arriving.
 */
object TourTargets {
    private val bounds = mutableStateMapOf<TourSpot, Pair<Any, Rect>>()

    fun boundsOf(spot: TourSpot): Rect? = bounds[spot]?.second

    fun isShown(spot: TourSpot): Boolean = spot in bounds

    internal fun put(
        spot: TourSpot,
        owner: Any,
        rect: Rect,
    ) {
        val now = bounds[spot]
        if (now == null || now.first === owner) {
            if (now?.second != rect) bounds[spot] = owner to rect
        }
    }

    internal fun remove(
        spot: TourSpot,
        owner: Any,
    ) {
        if (bounds[spot]?.first === owner) bounds.remove(spot)
    }
}

/**
 * Marks this control as [spot], so the tour can light it. Costs one position callback per layout
 * pass and nothing when no tour runs; a control that scrolls off screen or leaves composition
 * unregisters itself.
 */
fun Modifier.tourTarget(spot: TourSpot): Modifier = this then TourTargetElement(spot)

private data class TourTargetElement(
    val spot: TourSpot,
) : ModifierNodeElement<TourTargetNode>() {
    override fun create(): TourTargetNode = TourTargetNode(spot)

    override fun update(node: TourTargetNode) {
        node.retarget(spot)
    }
}

private class TourTargetNode(
    private var spot: TourSpot,
) : Modifier.Node(),
    GlobalPositionAwareModifierNode,
    CompositionLocalConsumerModifierNode {
    private val origin = IntArray(2)

    override fun onGloballyPositioned(coordinates: LayoutCoordinates) {
        if (!coordinates.isAttached) return
        val inWindow = coordinates.boundsInWindow()
        if (inWindow.isEmpty) {
            TourTargets.remove(spot, this)
            return
        }
        currentValueOf(LocalView).getLocationOnScreen(origin)
        TourTargets.put(spot, this, inWindow.translate(origin[0].toFloat(), origin[1].toFloat()))
    }

    override fun onDetach() {
        TourTargets.remove(spot, this)
    }

    fun retarget(next: TourSpot) {
        if (next == spot) return
        TourTargets.remove(spot, this)
        spot = next
    }
}
