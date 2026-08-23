package dev.geode.ui

import androidx.annotation.StringRes
import dev.geode.R

/**
 * What the person opening the app uses it for.
 *
 * Decides two things: which tab the app lands on, and whether Studio appears in the navigation
 * at all. Someone who only wants to listen should not have to walk past a render queue to reach
 * their music.
 *
 * First run used to ASK this, as its final step. It no longer does — the question made someone
 * categorise themselves before they had seen a single thing to categorise, and the honest answer
 * on a fresh install is almost always "I don't know yet". Everyone starts on [BOTH], which hides
 * nothing, and Settings > Behaviour narrows it once there is something to narrow.
 */
enum class UserIntent(
    @param:StringRes val labelRes: Int,
    @param:StringRes val detailRes: Int,
) {
    LISTENING(R.string.settings_intent_listening, R.string.settings_intent_listening_detail),
    MAKING_VIDEOS(R.string.settings_intent_videos, R.string.settings_intent_videos_detail),
    BOTH(R.string.settings_intent_both, R.string.settings_intent_both_detail),
    ;

    /** Studio is hidden for pure listeners; everyone else gets it. */
    val showsStudio: Boolean get() = this != LISTENING

    /** Where the app opens. Video-first users land in Studio, everyone else on the Stage. */
    val landingDestination: GeodeDestination
        get() =
            when (this) {
                LISTENING, BOTH -> GeodeDestination.PLAYER
                MAKING_VIDEOS -> GeodeDestination.STUDIO
            }
}
