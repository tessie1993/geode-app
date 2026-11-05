package dev.geode.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeDown
import androidx.compose.ui.test.swipeUp
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.google.common.truth.Truth.assertThat
import dev.geode.data.GuiPrefs
import dev.geode.ui.theme.StoneIcon
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Issue #240 (3A Visuals shell), one test per acceptance criterion the shell's own pieces carry:
 * the mini player's fade, pin, swipes and handle pill, the pill tab bar, and the options sheet's
 * heights and back order as [VisualsNavState] keeps them. The pieces are tested alone, not through
 * `MainActivity`: the visual's GL surface and the session would make the timing tests slow and
 * flaky, and every one of these decisions lives in the piece itself.
 */
@RunWith(AndroidJUnit4::class)
class VisualsShellTest {
    @get:Rule
    val compose = createComposeRule()

    // --- Mini player fades after 3 s by default; a tap pins it -------------------------------

    @Test
    fun miniPlayerFadesAfterTheDefaultThreeSeconds() {
        lateinit var chrome: ShellChromeState
        compose.mainClock.autoAdvance = false
        compose.setContent {
            chrome = rememberShellChrome(GuiPrefs.MINI_PLAYER_FADE_DEFAULT_SECONDS, held = false, Unit)
            Text(if (chrome.awake) AWAKE else ASLEEP)
        }
        compose.mainClock.advanceTimeBy(2_500)
        compose.onNodeWithText(AWAKE).assertExists()
        compose.mainClock.advanceTimeBy(1_000)
        compose.onNodeWithText(ASLEEP).assertExists()

        // A wake (a bottom tap on the visual, the handle pill) brings it back for another 3 s.
        compose.runOnUiThread { chrome.wake() }
        compose.mainClock.advanceTimeBy(1_000)
        compose.onNodeWithText(AWAKE).assertExists()
        compose.mainClock.advanceTimeBy(3_000)
        compose.onNodeWithText(ASLEEP).assertExists()
    }

    @Test
    fun aPinHoldsTheMiniPlayerPastTheFade() {
        lateinit var chrome: ShellChromeState
        compose.mainClock.autoAdvance = false
        compose.setContent {
            chrome = rememberShellChrome(GuiPrefs.MINI_PLAYER_FADE_DEFAULT_SECONDS, held = false, Unit)
            Text(if (chrome.awake) AWAKE else ASLEEP)
        }
        compose.runOnUiThread { chrome.pin() }
        compose.mainClock.advanceTimeBy(10_000)
        compose.onNodeWithText(AWAKE).assertExists()
        compose.mainClock.advanceTimeBy(ShellChromeState.PIN_MS)
        compose.onNodeWithText(ASLEEP).assertExists()
    }

    @Test
    fun aPipWindowKeepsTheChromeAsleep() {
        compose.setContent {
            val chrome = rememberShellChrome(GuiPrefs.MINI_PLAYER_FADE_DEFAULT_SECONDS, held = true, Unit)
            Text(if (chrome.awake) AWAKE else ASLEEP)
        }
        compose.onNodeWithText(ASLEEP).assertExists()
    }

    // --- Mini player: art, title · artist, play/pause, next; swipe up opens Player, down hides -

    @Test
    fun miniPlayerShowsTheTrackAndItsSwipesOpenAndHide() {
        var opened = 0
        var hidden = 0
        var played = 0
        var skipped = 0
        compose.setContent {
            MaterialTheme {
                FloatingMiniPlayer(
                    title = "Song",
                    artist = "Artist",
                    artworkUri = null,
                    isPlaying = false,
                    progress = { 0.5f },
                    opacity = 0.62f,
                    onPin = {},
                    onOpenPlayer = { opened++ },
                    onHide = { hidden++ },
                    onPlayPause = { played++ },
                    onNext = { skipped++ },
                    modifier = Modifier.testTag(MINI),
                )
            }
        }
        compose.onNodeWithText("Song").assertExists()
        compose.onNodeWithText("Artist").assertExists()
        compose.onNodeWithTag(MINI).performTouchInput { swipeUp() }
        compose.runOnIdle { assertThat(opened).isEqualTo(1) }
        compose.onNodeWithTag(MINI).performTouchInput { swipeDown() }
        compose.runOnIdle { assertThat(hidden).isEqualTo(1) }
        compose.onNode(hasContentDescription("Play")).performClick()
        compose.onNode(hasContentDescription("Next")).performClick()
        compose.runOnIdle {
            assertThat(played).isEqualTo(1)
            assertThat(skipped).isEqualTo(1)
        }
    }

    // --- When faded, a handle pill stays; a tap or a swipe up brings the player back ----------

    @Test
    fun theHandlePillWakesOnTapAndOnSwipeUp() {
        var woken = 0
        compose.setContent {
            MaterialTheme { MiniPlayerHandle(onWake = { woken++ }, modifier = Modifier.testTag(HANDLE)) }
        }
        compose.onNodeWithTag(HANDLE).performClick()
        compose.runOnIdle { assertThat(woken).isEqualTo(1) }
        compose.onNodeWithTag(HANDLE).performTouchInput { swipeUp() }
        compose.runOnIdle { assertThat(woken).isAtLeast(2) }
    }

    // --- Floating pill tab bar -----------------------------------------------------------------

    @Test
    fun theTabBarSelectsTheTabTapped() {
        var selected = -1
        compose.setContent {
            MaterialTheme {
                FloatingTabBar(
                    items = listOf(DockItem("Player", StoneIcon.PLAY), DockItem("Visuals", StoneIcon.NEXT)),
                    selected = 0,
                    onSelect = { selected = it },
                    opacity = 0.62f,
                )
            }
        }
        compose.onNodeWithText("Visuals").performClick()
        compose.runOnIdle { assertThat(selected).isEqualTo(1) }
    }

    // --- Sheet: peek / half / tall, ×/double tap hide, height remembered, Back through the nav --

    @Test
    fun aTabRaisesASheetDownToItsTabsToHalf() {
        lateinit var nav: VisualsNavState
        compose.setContent { nav = rememberVisualsNavState() }
        compose.runOnIdle {
            nav.settleSheet(SheetHeight.PEEK)
            nav.openTab(VisualsPage.PRESETS)
            assertThat(nav.sheet).isEqualTo(SheetHeight.HALF)
            assertThat(nav.top).isEqualTo(VisualsPage.PRESETS)
        }
    }

    @Test
    fun hidingAndShowingTheSheetComesBackAtTheLastHeight() {
        lateinit var nav: VisualsNavState
        compose.setContent { nav = rememberVisualsNavState() }
        compose.runOnIdle {
            nav.settleSheet(SheetHeight.TALL)
            // × and the double tap hide it; the double tap (or Back) brings it back where it was.
            nav.hideSheet()
            assertThat(nav.sheetShown).isFalse()
            nav.showSheet()
            assertThat(nav.sheet).isEqualTo(SheetHeight.TALL)
        }
    }

    @Test
    fun backBringsAHiddenSheetBackThenRetracesTheRouteThenLeaves() {
        lateinit var nav: VisualsNavState
        compose.setContent { nav = rememberVisualsNavState() }
        compose.runOnIdle {
            nav.openTab(VisualsPage.PRESETS)
            nav.openTab(VisualsPage.CUSTOMIZE)
            nav.settleSheet(SheetHeight.TALL)
            nav.hideSheet()

            assertThat(nav.back()).isTrue()
            assertThat(nav.sheet).isEqualTo(SheetHeight.TALL)
            assertThat(nav.back()).isTrue()
            assertThat(nav.top).isEqualTo(VisualsPage.PRESETS)
            assertThat(nav.back()).isTrue()
            assertThat(nav.top).isEqualTo(VisualsPage.STYLES)
            // Nothing left: the shell leaves Visuals.
            assertThat(nav.backEnabled).isFalse()
            assertThat(nav.back()).isFalse()
        }
    }

    @Test
    fun theSheetHeightAndRouteSurviveRecreation() {
        val restore = StateRestorationTester(compose)
        lateinit var nav: VisualsNavState
        restore.setContent { nav = rememberVisualsNavState() }
        compose.runOnIdle {
            nav.openTab(VisualsPage.MILKDROP)
            nav.settleSheet(SheetHeight.TALL)
            nav.hideSheet()
        }
        restore.emulateSavedInstanceStateRestore()
        compose.runOnIdle {
            assertThat(nav.sheet).isEqualTo(SheetHeight.HIDDEN)
            assertThat(nav.top).isEqualTo(VisualsPage.MILKDROP)
            nav.showSheet()
            assertThat(nav.sheet).isEqualTo(SheetHeight.TALL)
        }
    }

    private companion object {
        const val AWAKE = "awake"
        const val ASLEEP = "asleep"
        const val MINI = "mini"
        const val HANDLE = "handle"
    }
}
