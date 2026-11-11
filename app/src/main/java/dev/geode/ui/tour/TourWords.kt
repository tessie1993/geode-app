package dev.geode.ui.tour

import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.data.BehaviourPrefs
import dev.geode.data.CanvasGesture
import dev.geode.data.GestureAction
import dev.geode.model.GestureTip
import dev.geode.model.TourChapter
import dev.geode.model.TourSpot

/** A step's words: a title and a line or two, and the button's label where the step asks for an action. */
internal data class TourWords(
    @param:StringRes val title: Int,
    @param:StringRes val body: Int,
    @param:StringRes val action: Int? = null,
)

internal val TourSpot.words: TourWords
    get() =
        when (this) {
            TourSpot.NAV_LIBRARY ->
                TourWords(R.string.tour_nav_library_title, R.string.tour_nav_library_body, R.string.tour_nav_library_action)
            TourSpot.PLAY_SONG -> TourWords(R.string.tour_play_song, R.string.tour_play_song)
            TourSpot.NAV_VISUALS ->
                TourWords(R.string.tour_nav_visuals_title, R.string.tour_nav_visuals_body, R.string.tour_nav_visuals_action)
            TourSpot.VISUALS_TABS -> TourWords(R.string.tour_visuals_tabs_title, R.string.tour_visuals_tabs_body)
            TourSpot.STAGE_ARROWS -> TourWords(R.string.tour_stage_arrows_title, R.string.tour_stage_arrows_body)
            TourSpot.STAGE_TOOLS -> TourWords(R.string.tour_stage_tools_title, R.string.tour_stage_tools_body)
            TourSpot.STAGE_RECORD -> TourWords(R.string.tour_stage_record_title, R.string.tour_stage_record_body)
            TourSpot.CUSTOMIZE_KNOBS -> TourWords(R.string.tour_customize_knobs_title, R.string.tour_customize_knobs_body)
            TourSpot.CUSTOMIZE_LIST -> TourWords(R.string.tour_customize_list_title, R.string.tour_customize_list_body)
            TourSpot.CUSTOMIZE_RANDOMIZE ->
                TourWords(R.string.tour_customize_randomize_title, R.string.tour_customize_randomize_body)
            TourSpot.STUDIO_RENDER -> TourWords(R.string.tour_studio_render_title, R.string.tour_studio_render_body)
            TourSpot.STUDIO_MY_VIDEOS -> TourWords(R.string.tour_studio_videos_title, R.string.tour_studio_videos_body)
        }

@get:StringRes
internal val TourChapter.titleRes: Int
    get() =
        when (this) {
            TourChapter.START -> R.string.tour_chapter_start
            TourChapter.VISUALS -> R.string.tour_chapter_visuals
            TourChapter.CUSTOMIZE -> R.string.tour_chapter_customize
            TourChapter.STUDIO -> R.string.tour_chapter_studio
        }

@get:StringRes
internal val GestureTip.textRes: Int
    get() =
        when (this) {
            GestureTip.SWIPE_STYLES -> R.string.tour_tip_swipe_styles
            GestureTip.DOUBLE_TAP_SHEET -> R.string.tour_tip_double_tap_sheet
            GestureTip.HOLD_TO_RECORD -> R.string.tour_tip_hold_to_record
            GestureTip.SWIPE_UP_PLAYER -> R.string.tour_tip_swipe_up_player
            GestureTip.SWIPE_DOWN_KNOBS -> R.string.tour_tip_swipe_down_knobs
            GestureTip.SWIRL_RANDOMIZE -> R.string.tour_tip_swirl_randomize
        }

/** Whether [behaviour]'s gesture map still gives this tip's gesture its action: a tip never names a gesture the user moved. */
internal fun GestureTip.fits(behaviour: BehaviourPrefs): Boolean =
    when (this) {
        GestureTip.SWIPE_STYLES -> behaviour.actionFor(CanvasGesture.SWIPE_SIDEWAYS) == GestureAction.STEP_STYLE
        GestureTip.DOUBLE_TAP_SHEET -> behaviour.actionFor(CanvasGesture.DOUBLE_TAP) == GestureAction.TOGGLE_SHEET
        GestureTip.HOLD_TO_RECORD -> behaviour.actionFor(CanvasGesture.LONG_PRESS) == GestureAction.RECORD
        GestureTip.SWIPE_DOWN_KNOBS -> behaviour.actionFor(CanvasGesture.SWIPE_DOWN) == GestureAction.MACROS
        GestureTip.SWIRL_RANDOMIZE -> behaviour.actionFor(CanvasGesture.SWIRL) == GestureAction.RANDOMIZE_ALL
        // The mini player's own swipe, not one of the visual's.
        GestureTip.SWIPE_UP_PLAYER -> true
    }
