package dev.geode.data

import androidx.annotation.StringRes
import dev.geode.R

/**
 * What the person opening the app said they use it for. Retired: the profile is gone.
 *
 * Everyone gets the whole app, Studio included, and no screen asks this any more (first run
 * never did after it stopped being a question, and the Behaviour picker leaves with the Settings
 * restructure). The enum stays so a value saved by an older build still parses in [GuiPrefs] and
 * a backup that carries it still restores; [showsStudio] is true whatever was saved.
 */
enum class UserIntent(
    @param:StringRes val labelRes: Int,
    @param:StringRes val detailRes: Int,
) {
    LISTENING(R.string.settings_intent_listening, R.string.settings_intent_listening_detail),
    MAKING_VIDEOS(R.string.settings_intent_videos, R.string.settings_intent_videos_detail),
    BOTH(R.string.settings_intent_both, R.string.settings_intent_both_detail),
    ;

    /** Studio is always shown: nobody is filed as "only listening" any more. */
    val showsStudio: Boolean get() = true
}
