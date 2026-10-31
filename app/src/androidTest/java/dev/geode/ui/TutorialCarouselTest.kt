package dev.geode.ui

import androidx.annotation.StringRes
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import dev.geode.R
import dev.geode.model.TutorialChapter
import dev.geode.model.TutorialTour
import dev.geode.ui.theme.LocalReducedMotion
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The tour's carousel, driven the way [TutorialHost] drives it: it only reports moves, finishes
 * and Show me taps, and this test plays the host. Run under reduced motion as well as without,
 * since that changes how every button moves the pager.
 */
@RunWith(AndroidJUnit4::class)
class TutorialCarouselTest {
    @get:Rule
    val compose = createComposeRule()

    private val tour = TutorialTour(listOf(TutorialChapter.WELCOME, TutorialChapter.SETTINGS))
    private val moves = mutableListOf<Pair<Int, Int>>()
    private val completed = mutableListOf<TutorialChapter>()
    private val shown = mutableListOf<TutorialTarget>()
    private var finished = 0
    private var skippedAll = 0

    private fun text(
        @StringRes id: Int,
    ) = InstrumentationRegistry.getInstrumentation().targetContext.getString(id)

    private fun content(reducedMotion: Boolean): @Composable () -> Unit =
        {
            MaterialTheme {
                CompositionLocalProvider(LocalReducedMotion provides reducedMotion) {
                    TutorialCarousel(
                        tour = tour,
                        startPage = 0,
                        onMove = { from, to ->
                            moves += from to to
                            tour.completedBy(from, to)?.also { completed += it }
                        },
                        onFinish = { finished++ },
                        onSkipAll = { skippedAll++ },
                        onShowMe = { shown += it },
                    )
                }
            }
        }

    private fun show(reducedMotion: Boolean = true) = compose.setContent(content(reducedMotion))

    private fun click(
        @StringRes id: Int,
    ) = compose.onNodeWithText(text(id)).performClick()

    private fun assertShowing(
        @StringRes id: Int,
    ) = compose.onNodeWithText(text(id)).assertIsDisplayed()

    @Test
    fun next_and_back_walk_the_cards() {
        show()
        assertShowing(R.string.tutorial_welcome_1_title)
        click(R.string.tutorial_next)
        assertShowing(R.string.tutorial_welcome_2_title)
        click(R.string.tutorial_back)
        assertShowing(R.string.tutorial_welcome_1_title)
        assertEquals(listOf(0 to 1, 1 to 0), moves)
    }

    @Test
    fun next_animates_too_without_reduced_motion() {
        show(reducedMotion = false)
        click(R.string.tutorial_next)
        compose.waitForIdle()
        assertShowing(R.string.tutorial_welcome_2_title)
    }

    @Test
    fun leaving_a_chapter_from_its_last_card_completes_it() {
        show()
        click(R.string.tutorial_next)
        click(R.string.tutorial_next)
        assertShowing(R.string.tutorial_settings_1_title)
        assertEquals(listOf(TutorialChapter.WELCOME), completed)
    }

    @Test
    fun skip_chapter_jumps_ahead_without_completing() {
        show()
        click(R.string.tutorial_skip_chapter)
        assertShowing(R.string.tutorial_settings_1_title)
        assertTrue(completed.isEmpty())
    }

    @Test
    fun show_me_reports_the_cards_target() {
        show()
        click(R.string.tutorial_skip_chapter)
        click(R.string.tutorial_show_me)
        assertEquals(listOf(TutorialTarget.SETTINGS), shown)
    }

    @Test
    fun the_last_card_finishes_and_skip_all_ends_anywhere() {
        show()
        click(R.string.tutorial_skip_all)
        assertEquals(1, skippedAll)
        click(R.string.tutorial_skip_chapter)
        click(R.string.tutorial_next)
        click(R.string.tutorial_finish)
        assertEquals(1, finished)
    }

    @Test
    fun the_page_survives_recreation() {
        val restoration = StateRestorationTester(compose)
        restoration.setContent(content(reducedMotion = true))
        click(R.string.tutorial_next)
        restoration.emulateSavedInstanceStateRestore()
        assertShowing(R.string.tutorial_welcome_2_title)
    }
}
