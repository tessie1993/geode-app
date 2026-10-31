package dev.geode.ui

import androidx.annotation.StringRes
import androidx.compose.runtime.Immutable
import dev.geode.R
import dev.geode.model.TutorialChapter

/** Where a card's Show me goes. [AppShell] maps each to the real screen. */
enum class TutorialTarget { PLAYER, LIBRARY, VISUALS, LIVE_VIEW, STUDIO_RENDER, STUDIO_EDIT, SETTINGS, PAYWALL }

/**
 * One card of the tour: what Wave says, what the drawing shows, and where Show me goes.
 *
 * [pro] puts a Pro badge on the card. The free tour still shows Pro features, so nobody learns
 * about 4K export only from the paywall, but it says plainly which ones they are.
 */
@Immutable
data class TutorialCard(
    @param:StringRes val titleRes: Int,
    @param:StringRes val bodyRes: Int,
    val art: TutorialArt,
    val showMe: TutorialTarget? = null,
    val pro: Boolean = false,
)

@get:StringRes
val TutorialChapter.titleRes: Int
    get() =
        when (this) {
            TutorialChapter.WELCOME -> R.string.tutorial_chapter_welcome
            TutorialChapter.SETTINGS -> R.string.tutorial_chapter_settings
        }

/** The cards of each chapter. Exactly [TutorialChapter.cards] of them; `TutorialCardsTest` checks. */
val TutorialChapter.cardContent: List<TutorialCard>
    get() =
        when (this) {
            TutorialChapter.WELCOME ->
                listOf(
                    TutorialCard(R.string.tutorial_welcome_1_title, R.string.tutorial_welcome_1_body, TutorialArt.HELLO),
                    TutorialCard(R.string.tutorial_welcome_2_title, R.string.tutorial_welcome_2_body, TutorialArt.CHAPTERS),
                )
            TutorialChapter.SETTINGS ->
                listOf(
                    TutorialCard(
                        R.string.tutorial_settings_1_title,
                        R.string.tutorial_settings_1_body,
                        TutorialArt.GEAR,
                        TutorialTarget.SETTINGS,
                    ),
                    TutorialCard(R.string.tutorial_settings_2_title, R.string.tutorial_settings_2_body, TutorialArt.CONFETTI),
                )
        }
