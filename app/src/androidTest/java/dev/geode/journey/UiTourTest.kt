package dev.geode.journey

import android.Manifest
import android.app.PictureInPictureParams
import android.graphics.PointF
import android.os.Build
import android.os.SystemClock
import android.view.KeyEvent
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.performSemanticsAction
import androidx.compose.ui.test.performTextInput
import androidx.media3.common.Player
import androidx.media3.session.MediaBrowser
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import dev.geode.ui.MainActivity
import org.junit.After
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * The UI tour: walks every screen, sheet, page and gesture of the app on the emulator and leaves
 * numbered screenshots plus a report of what each step expected and saw, for a design audit to
 * read without a device. Not a check: nothing here asserts that the UI is right, only that the
 * app came up and did not crash (see [UiTour] for why every step is best-effort).
 *
 * Skipped unless the instrumentation argument `uitour=1` is set, so the ordinary emulator job
 * in android.yml, which runs this whole package, never pays for it; `.github/workflows/ui-tour.yml`
 * sets it and runs the class once per [TourConfig] (`uitourConfig=portrait|landscape|font`).
 *
 * One test per area, not one long walk, because Orchestrator runs each test in its own process
 * on a fresh install: a crash in Studio cannot take the Settings screenshots with it, and each
 * area starts from the state it is about ([TourCase]: the safety notice, the tour pointers, a
 * free user, a Pro one). The cases' screenshots still sort in walk order (`NNN_` from the case's
 * index).
 *
 * Labels are the app's English strings as a user reads them, matched the way the journeys match
 * them (text, else content description). Where a control has neither, the tour cannot reach it
 * and its row says so: that is a TalkBack finding in its own right.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class UiTourTest {
    private val setup = TourSetup()
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(setup).around(compose)

    private val tour by lazy { UiTour(compose, setup) }
    private var browser: MediaBrowser? = null

    @After
    fun release() {
        browser?.let { b -> runCatching { b.stopAndRelease() } }
    }

    // -- cases -------------------------------------------------------------------------------

    @Test
    @TourCase(index = 1, area = "first-run", onboarded = false)
    fun firstRun() {
        tour.requireShell("first-run", hasText(SAFETY_TITLE))
        tour.pages("first-run", "safety-notice", pages = 3)
        tour.step("first-run", "acknowledge", "the shell opens, with the first-run guide") {
            tour.tap(SAFETY_ACK)
            "shell up: ${tour.waitFor(navItem(PLAYER))}"
        }
        walkPointers("first-run", "guide")
    }

    /**
     * A user past the safety notice who has not seen the tour: the guided path (open Library,
     * play a song, open Visuals), then each screen's pointers the first time it lays out, then a
     * replay from Settings › Tutorials.
     */
    @Test
    @TourCase(index = 2, area = "pointers", tutorialSeen = false)
    fun tourPointers() {
        tour.requireShell("pointers", navItem(LIBRARY))
        tour.shot("pointers", "guide-start")
        tour.step("pointers", "guide-open-library", "the guide's next pointer") { tour.tap(LIBRARY) }
        walkPointers("pointers", "library")
        tour.step("pointers", "guide-play", "a song playing; the guide points at Visuals") { playTone() }
        tour.step("pointers", "guide-open-visuals", "Visuals' pointers") { tour.tap(VISUALS) }
        walkPointers("pointers", "visuals")
        for (dest in listOf(STUDIO, SETTINGS, PLAYER)) {
            tour.step("pointers", "open-${slug(dest)}", "$dest's pointers, first time") { tour.tap(dest) }
            walkPointers("pointers", slug(dest))
        }
        tour.step("pointers", "settings-tutorials", "Settings › Tutorials, a row per chapter") {
            tour.tap(SETTINGS)
            tour.tap(TUTORIALS)
            null
        }
        tour.pages("pointers", "tutorials-list", pages = 2)
        tour.step("pointers", "replay-customize", "the Customize chapter replays on Visuals") {
            tour.tap(chapterRow(CUSTOMIZE))
            null
        }
        walkPointers("pointers", "replay-customize")
    }

    @Test
    @TourCase(index = 3, area = "player-library")
    fun playerAndLibrary() {
        tour.requireShell("player", hasText(EMPTY_PLAYER))
        tour.pages("player", "empty", pages = 2)
        tour.step("player", "more-card", "the More card's quick actions") { tour.tap(MORE) }

        tour.step("library", "open", "the music-access rationale (permission not granted yet)") { tour.tap(LIBRARY) }
        tour.step("library", "allow-music", "Android's permission dialog over the app") { tour.tap(ALLOW_MUSIC) }
        tour.step("library", "granted", "the library, empty on the emulator") {
            grantMusic()
            tour.back()
            tour.tap(PLAYER)
            tour.tap(LIBRARY)
            null
        }
        for (tab in LIBRARY_TABS) {
            tour.step("library", "tab-${slug(tab)}", "the $tab tab") { tour.tap(tab) }
        }
        tour.step("library", "search", "the search field with a query typed") {
            tour.tapIfPresent(SEARCH)
            compose.onAllNodes(hasSetTextAction()).onFirst().performTextInput("tone")
            null
        }
        tour.back()
        tour.back()

        tour.step("player", "playing", "the Player with the test tone playing") {
            val played = playTone()
            tour.tap(PLAYER)
            played
        }
        tour.pages("player", "playing-scroll", pages = 3)
        tour.step("player", "queue-sheet", "the queue sheet") {
            if (!tour.tapIfPresent(OPEN_QUEUE)) tour.tap(UP_NEXT)
            null
        }
        tour.step("player", "queue-save-dialog", "the save-as-playlist dialog") { tour.tap(SAVE_AS_PLAYLIST) }
        tour.back()
        tour.back()
        tour.shot("player", "queue-closed")
    }

    /** Visuals as a free user: every sheet height, every page, the overlays and back's order. */
    @Test
    @TourCase(index = 4, area = "visuals")
    fun visuals() {
        tour.requireShell("visuals", hasText(EMPTY_PLAYER))
        tour.step("visuals", "open", "Visuals with the sheet at its saved height") {
            playTone()
            tour.tap(VISUALS)
            tour.waitFor(hasContentDescription(LIVE_VISUAL))
            tour.state().summary()
        }
        tour.measureWindow()
        if (tour.config.landscape) panelSizes() else sheetHeights()

        styles()
        customizeFree()
        presets()
        milkDrop()
        backOrder()
        overlays()
    }

    /** Customize as a Pro user, so every sub-page opens rather than the paywall. */
    @Test
    @TourCase(index = 5, area = "customize", pro = true)
    fun customize() {
        tour.requireShell("customize", hasText(EMPTY_PLAYER))
        tour.step("customize", "open", "Customize, sheet tall") {
            tour.tap(VISUALS)
            tour.tap(CUSTOMIZE)
            sheetTall()
            tour.state().summary()
        }
        for ((page, subs) in CUSTOMIZE_PAGES) {
            tour.step("customize", "page-${slug(page)}", "the jump header scrolls to $page") { tour.tap(page) }
            tour.pages("customize", "page-${slug(page)}-body", pages = 2)
            for (sub in subs) {
                tour.step("customize", "sub-${slug(sub)}", "the $sub page") { tour.tap(sub) }
                tour.pages("customize", "sub-${slug(sub)}-body", pages = 3)
                tour.step("customize", "sub-${slug(sub)}-back", "back closes $sub, the page list again") { tour.back() }
            }
        }
    }

    /**
     * Every gesture on the visual as a free user, then the mini player's own gestures, then the
     * edge-started variants, last because an edge swipe can send the app home.
     */
    @Test
    @TourCase(index = 6, area = "gestures")
    fun gestures() {
        tour.requireShell("gestures", hasText(EMPTY_PLAYER))
        openStageWithTone("gestures")
        for (g in tour.visualGestures()) tour.runGesture("gesture", g) { tour.stageBaseline() }
        miniPlayerGestures()
        for (g in tour.edgeGestures()) tour.runGesture("edge", g) { tour.stageBaseline() }
    }

    /** The Pro side of the gestures (a hold records, two circles randomize), then PiP. */
    @Test
    @TourCase(index = 7, area = "gestures-pro", pro = true)
    fun gesturesProAndPip() {
        tour.requireShell("gestures-pro", hasText(EMPTY_PLAYER))
        openStageWithTone("gestures-pro")
        val wanted = setOf("hold", "swirl")
        for (g in tour.visualGestures().filter { it.name in wanted }) {
            tour.runGesture("gesture-pro", g) { tour.stageBaseline() }
            if (g.name == "hold") {
                tour.pages("gesture-pro", "record-save-sheet", pages = 1)
                tour.step("gesture-pro", "stop-take", "the take stops") {
                    tour.back()
                    tour.tapIfPresent(UiTour.STOP_TAKE)
                    tour.state().summary()
                }
            }
        }
        pictureInPicture()
    }

    @Test
    @TourCase(index = 8, area = "studio")
    fun studio() {
        tour.requireShell("studio", hasText(EMPTY_PLAYER))
        tour.step("studio", "open", "Studio › Render, the song playing as its source") {
            playTone()
            tour.tap(STUDIO)
            tour.tapIfPresent(studioTab(RENDER))
            null
        }
        tour.pages("studio", "render", pages = 5)
        tour.step("studio", "source-menu", "the Source dropdown") { tour.tap(DROPDOWN) }
        for (scope in RENDER_SCOPES) {
            tour.step("studio", "source-${slug(scope)}", "the $scope source (a picker, or the paywall for a batch)") {
                tour.tap(DROPDOWN)
                tour.tap(scope)
                null
            }
            tour.back()
        }
        for (option in RENDER_OPTIONS) {
            tour.step("studio", "option-${slug(option)}", "$option chosen or opened") { tour.tap(option) }
            if (option in RENDER_OPTIONS_THAT_OPEN) tour.back()
        }
        tour.step("studio", "my-videos", "My videos, empty") { tour.tap(studioTab(MY_VIDEOS)) }
        for (chip in MY_VIDEOS_CHIPS) {
            tour.step("studio", "my-videos-${slug(chip)}", "the $chip filter") { tour.tap(chip) }
        }
    }

    /** Every Settings tab, scrolled to its end, with the dialogs and drill-ins under them, then Go Pro. */
    @Test
    @TourCase(index = 9, area = "settings")
    fun settings() {
        tour.requireShell("settings", hasText(EMPTY_PLAYER))
        tour.step("settings", "open", "Settings, the Go Pro row on top") { tour.tap(SETTINGS) }
        for (tab in SETTINGS_TABS) {
            tour.step("settings", "tab-${slug(tab)}", "the $tab tab") { tour.tap(tab) }
            tour.pages("settings", slug(tab), pages = 5)
            settingsDrillIns(tab)
        }
        tour.step("settings", "go-pro", "the paywall from Settings' Go Pro row") {
            tour.tap(GO_PRO)
            "paywall=${tour.waitFor(hasText(UiTour.PAYWALL_RESTORE))}"
        }
        tour.pages("settings", "paywall", pages = 4)
        tour.step("settings", "paywall-close", "the paywall closes") { tour.tap(UiTour.PAYWALL_CLOSE) }
    }

    // -- Visuals -----------------------------------------------------------------------------

    /**
     * Each height three ways: dragging the handle (what a finger does), a tab tap (which raises a
     * peeking sheet to half) and the handle's TalkBack actions. Every row records where the
     * handle came to rest.
     */
    private fun sheetHeights() {
        val drags = listOf("tall" to 0.12f, "half" to 0.5f, "peek" to 0.9f, "hidden" to 0.985f)
        for ((name, fy) in drags) {
            tour.step("sheet", "drag-$name", "the sheet settles $name") { dragSheetTo(fy) }
        }
        tour.step("sheet", "back-from-hidden", "back brings a hidden sheet back") {
            tour.back()
            tour.state().summary()
        }
        tour.step("sheet", "actions-to-peek", "collapse, collapse: down to the tabs") {
            repeat(2) { tour.customAction(UiTour.SHEET_COLLAPSE) }
            tour.state().summary()
        }
        tour.step("sheet", "tab-raises-to-half", "a tab tap on a peeking sheet raises it to half") {
            tour.tap(PRESETS)
            tour.state().summary()
        }
        for ((label, name) in listOf(UiTour.SHEET_EXPAND to "expand", UiTour.SHEET_COLLAPSE to "collapse")) {
            tour.step("sheet", "action-$name", "TalkBack's $label") {
                tour.customAction(label)
                tour.state().summary()
            }
        }
        tour.step("sheet", "action-close", "TalkBack's close hides it") {
            tour.customAction(UiTour.SHEET_CLOSE)
            tour.state().summary()
        }
        tour.step("sheet", "double-tap-back", "a double tap on the visual brings it back") {
            TourTouch.doubleTap(tour.at(0.5f, 0.4f))
            SystemClock.sleep(UiTour.SHORT_SETTLE_MS)
            tour.state().summary()
        }
        tour.tapIfPresent(STYLES)
    }

    /** Landscape's side panel: its edge's TalkBack actions and a drag of the edge itself. */
    private fun panelSizes() {
        for ((label, name) in listOf(UiTour.PANEL_WIDER to "wider", UiTour.PANEL_NARROWER to "narrower")) {
            tour.step("panel", "action-$name", "TalkBack's $label") {
                tour.customAction(label)
                tour.state().summary()
            }
        }
        tour.step("panel", "drag-edge", "dragging the edge toward the panel narrows it") {
            val edge = checkNotNull(tour.bounds(hasContentDescription(UiTour.PANEL_HANDLE))) { "no panel edge" }
            val from = PointF(edge.center.x, edge.center.y)
            TourTouch.swipe(from, PointF(from.x + tour.window.width * 0.15f, from.y))
            tour.state().summary()
        }
        tour.step("panel", "close", "the × hides the panel") {
            tour.hideSheet()
            tour.state().summary()
        }
        tour.step("panel", "back-from-hidden", "back brings it back") {
            tour.back()
            tour.state().summary()
        }
        tour.tapIfPresent(STYLES)
    }

    private fun styles() {
        tour.step("styles", "open", "the Styles list") {
            tour.tap(STYLES)
            sheetTall()
            null
        }
        tour.pages("styles", "list", pages = 3)
        tour.step("styles", "group-dropdown", "the group dropdown open") { tour.tap(STYLES_ALL) }
        tour.step("styles", "group-flowing", "the Flowing & liquid group") { tour.tap(FLOWING) }
        tour.step("styles", "locked-style", "a Pro style plays while the paywall offers it") {
            tour.tap(LOCKED_STYLE)
            "paywall=${tour.waitFor(hasText(UiTour.PAYWALL_RESTORE))}"
        }
        tour.pages("styles", "paywall", pages = 4)
        tour.step("styles", "paywall-close", "the paywall closes and the style goes back") {
            tour.tap(UiTour.PAYWALL_CLOSE)
            tour.state().summary()
        }
        tour.step("styles", "group-all-again", "back to All styles") {
            tour.tap(FLOWING)
            tour.tap(STYLES_ALL)
            null
        }
        tour.step("styles", "row-menu", "a style's long-press menu") {
            compose.onAllNodes(LONG_PRESSABLE).onFirst().performSemanticsAction(SemanticsActions.OnLongClick)
            null
        }
        tour.back()
        tour.step("styles", "search", "style search with a query") {
            tour.tap(SEARCH_STYLES)
            compose.onAllNodes(hasSetTextAction()).onFirst().performTextInput("neon")
            null
        }
        tour.back()
        tour.back()
    }

    private fun customizeFree() {
        tour.step("customize-free", "open", "Customize as a free user") {
            tour.tap(CUSTOMIZE)
            sheetTall()
            null
        }
        tour.pages("customize-free", "top", pages = 2)
        for (page in CUSTOMIZE_PAGES.keys) {
            tour.step("customize-free", "page-${slug(page)}", "the jump header scrolls to $page") { tour.tap(page) }
        }
        tour.step("customize-free", "pro-sub-page", "a Pro sub-page opens the paywall") {
            tour.tap(MOTION)
            tour.tap(CAMERA)
            "paywall=${tour.waitFor(hasText(UiTour.PAYWALL_RESTORE))}"
        }
        tour.tapIfPresent(UiTour.PAYWALL_CLOSE)
        tour.step("customize-free", "free-sub-page", "Sets & macros, the free sub-page") {
            tour.tap(MACROS)
            tour.tap(SETS_AND_MACROS)
            null
        }
        tour.back()
    }

    private fun presets() {
        tour.step("presets", "open", "the Presets grid") { tour.tap(PRESETS) }
        tour.pages("presets", "grid", pages = 3)
        tour.step("presets", "card-sheet", "a card's long-press sheet") {
            compose.onAllNodes(LONG_PRESSABLE).onFirst().performSemanticsAction(SemanticsActions.OnLongClick)
            null
        }
        tour.back()
        tour.step("presets", "save-dialog", "Save current look's dialog") { tour.tap(SAVE_LOOK) }
        tour.back()
        tour.back()
    }

    private fun milkDrop() {
        tour.step("milkdrop", "open", "the MilkDrop overview cards") { tour.tap(MILKDROP) }
        tour.pages("milkdrop", "cards", pages = 3)
        tour.step("milkdrop", "textures", "Textures, opened from MilkDrop") { tour.tap(TEXTURES) }
        tour.pages("milkdrop", "textures-page", pages = 2)
    }

    /**
     * Back from the deepest point the walk reached (Textures, over MilkDrop, Presets, Customize
     * and Styles), one press at a time, recording where each lands: the route in reverse, then
     * Visuals is left.
     */
    private fun backOrder() {
        for (i in 1..MAX_BACKS) {
            val pressed =
                tour.step("back", "press-$i", "one level up the route; Styles last, then Visuals is left") {
                    val before = tour.state()
                    tour.back()
                    tour.state().changesFrom(before).ifEmpty { listOf("nothing changed") }.joinToString("; ")
                }
            if (!pressed || !tour.has(hasContentDescription(LIVE_VISUAL))) break
        }
        tour.ensureForeground()
        tour.tapIfPresent(VISUALS)
    }

    /** Quick settings (a tap on the top of the visual) and the Macros drawer (a swipe down). */
    private fun overlays() {
        tour.step("quick-settings", "open", "Visual settings over the visual") {
            tour.stageBaseline()
            TourTouch.tap(tour.at(0.5f, 0.2f))
            "open=${tour.waitFor(hasText(UiTour.QUICK_SETTINGS_TITLE))}"
        }
        tour.pages("quick-settings", "body", pages = 3)
        tour.back()
        tour.step("macros", "open", "the Macros drawer") {
            tour.stageBaseline()
            TourTouch.swipe(tour.at(0.5f, 0.24f), tour.at(0.5f, 0.6f))
            "open=${tour.waitFor(hasContentDescription(UiTour.MACROS_EDIT))}"
        }
        tour.step("macros", "edit", "its edit icon opens Customize's macros") { tour.tap(UiTour.MACROS_EDIT) }
        tour.back()
    }

    // -- gestures ----------------------------------------------------------------------------

    private fun openStageWithTone(area: String) {
        tour.step(area, "stage", "the visual alone, the tone playing, chrome asleep") {
            playTone()
            tour.tap(VISUALS)
            tour.waitFor(hasContentDescription(LIVE_VISUAL))
            tour.measureWindow()
            tour.stageBaseline()
            tour.state().summary()
        }
    }

    /** The floating mini player: a tap pins it, a swipe down hides it, a swipe up opens Player. */
    private fun miniPlayerGestures() {
        val mini = hasCustomAction(UiTour.MINI_PLAYER_OPEN)
        val cases =
            listOf(
                Triple("tap", "the player stays shown (pinned)") { c: PointF -> TourTouch.tap(c) },
                Triple("swipe-down", "the player hides") { c: PointF -> TourTouch.swipe(c, PointF(c.x, c.y + SWIPE_PX * dp())) },
                Triple("swipe-up", "Player opens") { c: PointF -> TourTouch.swipe(c, PointF(c.x, c.y - SWIPE_PX * dp())) },
            )
        for ((name, expected, drive) in cases) {
            tour.step("mini-player", name, expected) {
                tour.stageBaseline()
                TourTouch.tap(tour.at(0.5f, 0.6f))
                check(tour.waitFor(mini)) { "the mini player did not wake" }
                val box = checkNotNull(tour.bounds(mini))
                val before = tour.state()
                drive(PointF(box.center.x, box.center.y))
                SystemClock.sleep(UiTour.SHORT_SETTLE_MS * 2)
                tour.state().changesFrom(before).ifEmpty { listOf("nothing changed") }.joinToString("; ")
            }
        }
        tour.step("mini-player", "back-to-visuals", "Visuals again") { tour.tap(VISUALS) }
    }

    /**
     * Picture-in-picture two ways: asked for directly, and the way the app enters it below
     * Android 12 (home while Visuals plays, `onUserLeaveHint`). Each is followed by bringing the
     * app back, which a PiP window answers by expanding.
     */
    private fun pictureInPicture() {
        tour.step("pip", "enter", "the visual alone in a PiP window") {
            tour.stageBaseline()
            val activity = compose.activity
            val entered = onMain { activity.enterPictureInPictureMode(PictureInPictureParams.Builder().build()) }
            SystemClock.sleep(PIP_SETTLE_MS)
            "entered=$entered inPip=${onMain { activity.isInPictureInPictureMode }}"
        }
        tour.step("pip", "expand", "the app back at full size") {
            tour.ensureForeground()
            tour.state().summary()
        }
        tour.step("pip", "home-key", "home while Visuals plays enters PiP by itself") {
            val activity = compose.activity
            TourTouch.key(KeyEvent.KEYCODE_HOME)
            SystemClock.sleep(PIP_SETTLE_MS)
            "inPip=${onMain { activity.isInPictureInPictureMode }} focus=${tour.focus()}"
        }
        tour.step("pip", "home-expand", "the app back at full size") {
            tour.ensureForeground()
            tour.state().summary()
        }
    }

    // -- helpers -----------------------------------------------------------------------------

    private fun settingsDrillIns(tab: String) {
        when (tab) {
            AUDIO -> tour.step("settings", "audio-advanced", "the sound tuning's Advanced controls") { tour.tap(ADVANCED) }
            ABOUT -> aboutDialogs()
            EXPORT -> exportLink()
            else -> Unit
        }
    }

    private fun aboutDialogs() {
        for (dialog in listOf(LICENSES, PRIVACY)) {
            tour.step("settings", "about-${slug(dialog)}", "the $dialog dialog") { tour.tap(dialog) }
            tour.pages("settings", "about-${slug(dialog)}-body", pages = 2)
            tour.tapIfPresent(CLOSE)
        }
    }

    private fun exportLink() {
        tour.step("settings", "export-open-studio", "Settings › Export opens Studio › Render") { tour.tap(OPEN_STUDIO_RENDER) }
        tour.step("settings", "export-return", "back in Settings, Export tab") {
            tour.tap(SETTINGS)
            tour.tap(EXPORT)
            null
        }
    }

    /**
     * Steps through the tour pointers on screen: a screenshot of each, Next until Done. Gives up
     * quietly when no pointer is showing, since a screen may have queued none.
     */
    private fun walkPointers(
        area: String,
        name: String,
    ) {
        for (i in 1..MAX_POINTERS) {
            tour.shot(area, "$name-pointer$i")
            val next = hasText(TOUR_NEXT) and hasClickAction()
            val done = hasText(TOUR_DONE) and hasClickAction()
            val label =
                when {
                    tour.waitFor(next, POINTER_WAIT_MS) -> TOUR_NEXT
                    tour.has(done) -> TOUR_DONE
                    else -> null
                }
            if (label == null) return
            tour.step(area, "$name-pointer$i-${slug(label)}", "the next pointer, or the screen once done") {
                tour.tap(hasText(label))
                null
            }
            if (label == TOUR_DONE) return
        }
    }

    /** Drags the sheet's handle to [fy] of the window height and reports where it settled. */
    private fun dragSheetTo(fy: Float): String {
        val handle = checkNotNull(tour.bounds(hasContentDescription(UiTour.SHEET_HANDLE))) { "no sheet handle (hidden?)" }
        val from = PointF(handle.center.x, handle.top + HANDLE_GRAB_DP * dp())
        TourTouch.swipe(from, PointF(from.x, tour.window.height * fy), steps = DRAG_STEPS, stepMs = DRAG_STEP_MS)
        SystemClock.sleep(UiTour.SHORT_SETTLE_MS)
        return tour.state().summary()
    }

    /** Raises a portrait sheet to tall for the pages with long lists; the side panel has no heights. */
    private fun sheetTall() {
        if (tour.config.landscape) return
        repeat(2) { if (tour.state().sheet != "tall") tour.customAction(UiTour.SHEET_EXPAND) }
    }

    /**
     * The test tone through PlaybackService, on repeat, so the visual has something to hear and
     * the mini player something to show for the rest of the case.
     */
    private fun playTone(): String {
        val b = browser ?: connectBrowser().also { browser = it }
        val item = TestTrack.mediaItem(TestTrack.write(seconds = TONE_SECONDS))
        onMain {
            b.repeatMode = Player.REPEAT_MODE_ONE
            b.setMediaItem(item)
            b.prepare()
            b.play()
        }
        awaitMain("the tone to play") { b.isPlaying }
        return "tone playing"
    }

    /** Music access, granted from the shell as a user who said yes would have. */
    private fun grantMusic() {
        val permission =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                Manifest.permission.READ_MEDIA_AUDIO
            } else {
                Manifest.permission.READ_EXTERNAL_STORAGE
            }
        TourDevice.shell("pm grant ${compose.activity.packageName} $permission")
    }

    private fun dp(): Float = tour.window.density

    private fun navItem(label: String) = hasText(label) or hasContentDescription(label)

    /** A Studio tab, not the Render button below the stack that shares its name. */
    private fun studioTab(label: String) = hasText(label) and SemanticsMatcher.keyIsDefined(SemanticsProperties.Selected)

    /** A Settings › Tutorials row (it carries a done / not done state), not the nav item of that name. */
    private fun chapterRow(label: String) = hasText(label) and SemanticsMatcher.keyIsDefined(SemanticsProperties.StateDescription)

    private companion object {
        // The app's English strings (res/values), as the user reads them.
        const val SAFETY_TITLE = "Before the visuals start"
        const val SAFETY_ACK = "I understand"
        const val EMPTY_PLAYER = "Pick something to play"
        const val PLAYER = "Player"
        const val LIBRARY = "Library"
        const val STUDIO = "Studio"
        const val SETTINGS = "Settings"
        const val MORE = "More"
        const val ALLOW_MUSIC = "Allow music access"
        const val SEARCH = "Search"
        const val OPEN_QUEUE = "Open queue"
        const val UP_NEXT = "Up next"
        const val SAVE_AS_PLAYLIST = "Save as playlist"
        const val STYLES = "Styles"
        const val CUSTOMIZE = "Customize"
        const val PRESETS = "Presets"
        const val MILKDROP = "MilkDrop"
        const val TEXTURES = "Textures"
        const val STYLES_ALL = "All styles"
        const val FLOWING = "Flowing & liquid"
        const val LOCKED_STYLE = "Halvorsen Web"
        const val SEARCH_STYLES = "Search styles"
        const val SAVE_LOOK = "Save current look"
        const val MACROS = "Macros"
        const val MOTION = "Motion"
        const val CAMERA = "Camera"
        const val SETS_AND_MACROS = "Sets & macros"
        const val TOUR_NEXT = "Next"
        const val TOUR_DONE = "Done"
        const val TUTORIALS = "Tutorials"
        const val RENDER = "Render"
        const val MY_VIDEOS = "My videos"
        const val AUDIO = "Audio"
        const val ABOUT = "About"
        const val EXPORT = "Export"
        const val ADVANCED = "Advanced"
        const val LICENSES = "Open source licenses"
        const val PRIVACY = "Privacy policy"
        const val CLOSE = "Close"
        const val OPEN_STUDIO_RENDER = "Open Studio › Render"
        const val GO_PRO = "Go Pro"

        val LIBRARY_TABS = listOf("Tracks", "Albums", "Artists", "Folders", "Playlists", "Duplicates")
        val SETTINGS_TABS =
            listOf("Look", AUDIO, "Lyrics", EXPORT, "Folders", "Behavior", "Performance", "Help", TUTORIALS, "Backup", ABOUT)
        val RENDER_SCOPES = listOf("Play queue", "Playlist", "Chosen tracks")
        val RENDER_OPTIONS = listOf("Made for", "Segment", "Whole track", "Loop", "Ask where each time", "Videos library (Movies/MuzViz)")
        val RENDER_OPTIONS_THAT_OPEN = setOf("Made for", "Loop")
        val MY_VIDEOS_CHIPS = listOf("Renders", "Takes", "All")

        /**
         * Customize's pages in jump-header order, each with its sub-pages: `CustomizeSubPage` in
         * `ui/CustomizeAdvanced.kt` by title (the modulation matrix is not offered yet).
         */
        val CUSTOMIZE_PAGES =
            linkedMapOf(
                MACROS to listOf(SETS_AND_MACROS),
                "Look" to listOf("Make a palette"),
                "Style" to emptyList<String>(),
                MOTION to listOf(CAMERA, "Liquid", "Water ripples"),
                "Music" to listOf("How it listens", "Notes & key"),
                "Modulate" to listOf("Reactions", "Envelopes", "Waves"),
                ADVANCED to listOf("Layers", "Transitions", "Shader code"),
            )

        val LONG_PRESSABLE = SemanticsMatcher.keyIsDefined(SemanticsActions.OnLongClick)

        /** Studio's Source chooser, the first dropdown in the Render stack. */
        val DROPDOWN = SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.DropdownList)

        const val TONE_SECONDS = 60.0
        const val MAX_BACKS = 8
        const val MAX_POINTERS = 6
        const val POINTER_WAIT_MS = 3_000L
        const val PIP_SETTLE_MS = 2_500L
        const val HANDLE_GRAB_DP = 8f
        const val SWIPE_PX = 120f
        const val DRAG_STEPS = 30
        const val DRAG_STEP_MS = 16L
    }
}
