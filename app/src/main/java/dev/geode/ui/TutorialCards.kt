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
            TutorialChapter.PLAYER -> R.string.tutorial_chapter_player
            TutorialChapter.LIBRARY -> R.string.tutorial_chapter_library
            TutorialChapter.VISUALS -> R.string.tutorial_chapter_visuals
            TutorialChapter.CUSTOMIZE -> R.string.tutorial_chapter_customize
            TutorialChapter.LIVE_VIEW -> R.string.tutorial_chapter_live_view
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
            TutorialChapter.PLAYER ->
                listOf(
                    TutorialCard(
                        R.string.tutorial_player_1_title,
                        R.string.tutorial_player_1_body,
                        TutorialArt.TRANSPORT,
                        TutorialTarget.PLAYER,
                    ),
                    TutorialCard(
                        R.string.tutorial_player_2_title,
                        R.string.tutorial_player_2_body,
                        TutorialArt.LYRICS,
                        TutorialTarget.PLAYER,
                    ),
                    TutorialCard(
                        R.string.tutorial_player_3_title,
                        R.string.tutorial_player_3_body,
                        TutorialArt.TIMER,
                        TutorialTarget.PLAYER,
                    ),
                )
            TutorialChapter.LIBRARY ->
                listOf(
                    TutorialCard(
                        R.string.tutorial_library_1_title,
                        R.string.tutorial_library_1_body,
                        TutorialArt.ALBUMS,
                        TutorialTarget.LIBRARY,
                    ),
                    TutorialCard(
                        R.string.tutorial_library_2_title,
                        R.string.tutorial_library_2_body,
                        TutorialArt.LIST,
                        TutorialTarget.LIBRARY,
                    ),
                    TutorialCard(
                        R.string.tutorial_library_3_title,
                        R.string.tutorial_library_3_body,
                        TutorialArt.SEARCH,
                        TutorialTarget.LIBRARY,
                    ),
                )
            TutorialChapter.VISUALS ->
                listOf(
                    TutorialCard(
                        R.string.tutorial_visuals_1_title,
                        R.string.tutorial_visuals_1_body,
                        TutorialArt.STYLES,
                        TutorialTarget.VISUALS,
                    ),
                    TutorialCard(
                        R.string.tutorial_visuals_2_title,
                        R.string.tutorial_visuals_2_body,
                        TutorialArt.MORPH,
                        TutorialTarget.VISUALS,
                    ),
                    TutorialCard(
                        R.string.tutorial_visuals_3_title,
                        R.string.tutorial_visuals_3_body,
                        TutorialArt.AUTO,
                        TutorialTarget.VISUALS,
                    ),
                )
            TutorialChapter.CUSTOMIZE ->
                listOf(
                    TutorialCard(
                        R.string.tutorial_customize_1_title,
                        R.string.tutorial_customize_1_body,
                        TutorialArt.SLIDERS,
                        TutorialTarget.VISUALS,
                    ),
                    TutorialCard(
                        R.string.tutorial_customize_2_title,
                        R.string.tutorial_customize_2_body,
                        TutorialArt.BURST,
                        TutorialTarget.VISUALS,
                    ),
                    TutorialCard(
                        R.string.tutorial_customize_3_title,
                        R.string.tutorial_customize_3_body,
                        TutorialArt.DETAIL,
                        TutorialTarget.VISUALS,
                    ),
                    TutorialCard(
                        R.string.tutorial_customize_4_title,
                        R.string.tutorial_customize_4_body,
                        TutorialArt.DICE,
                        TutorialTarget.VISUALS,
                    ),
                )
            TutorialChapter.LIVE_VIEW ->
                listOf(
                    TutorialCard(
                        R.string.tutorial_live_1_title,
                        R.string.tutorial_live_1_body,
                        TutorialArt.EXPAND,
                        TutorialTarget.LIVE_VIEW,
                    ),
                    TutorialCard(
                        R.string.tutorial_live_2_title,
                        R.string.tutorial_live_2_body,
                        TutorialArt.TOUCH,
                        TutorialTarget.LIVE_VIEW,
                    ),
                    TutorialCard(R.string.tutorial_live_3_title, R.string.tutorial_live_3_body, TutorialArt.PIP, TutorialTarget.LIVE_VIEW),
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
