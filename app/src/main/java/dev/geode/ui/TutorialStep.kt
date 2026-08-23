package dev.geode.ui

import androidx.annotation.StringRes
import dev.geode.R

/**
 * One stop on the walkthrough.
 *
 * [destination] is the point: the tour NAVIGATES the app rather than describing it, so the thing
 * being explained is on screen behind the card while it is explained. A slideshow of screenshots
 * would go stale the first time a tab moved; this cannot, because it is the app.
 *
 * [requiresStudio] marks the steps that describe a tab someone may have hidden — the tour must
 * not point at something that is not there.
 */
enum class TutorialStep(
    val destination: GeodeDestination,
    @param:StringRes val titleRes: Int,
    @param:StringRes val bodyRes: Int,
    val requiresStudio: Boolean = false,
) {
    PLAYER(GeodeDestination.PLAYER, R.string.tutorial_player_title, R.string.tutorial_player_body),
    LIBRARY(GeodeDestination.LIBRARY, R.string.tutorial_library_title, R.string.tutorial_library_body),
    VISUALS(GeodeDestination.VISUALS, R.string.tutorial_visuals_title, R.string.tutorial_visuals_body),

    /**
     * Touch gets its own step on the Visuals tab rather than sharing the one above.
     *
     * It is the least discoverable thing in the app — nothing on screen suggests that a finger
     * does anything — and it is the feature most likely to be missed entirely.
     */
    TOUCH(GeodeDestination.VISUALS, R.string.tutorial_touch_title, R.string.tutorial_touch_body),
    STUDIO(GeodeDestination.STUDIO, R.string.tutorial_studio_title, R.string.tutorial_studio_body, requiresStudio = true),
    SETTINGS(GeodeDestination.SETTINGS, R.string.tutorial_settings_title, R.string.tutorial_settings_body),
    ;

    companion object {
        /** The steps that apply to a person whose navigation does or does not include Studio. */
        fun forNav(showsStudio: Boolean): List<TutorialStep> = entries.filter { showsStudio || !it.requiresStudio }
    }
}
