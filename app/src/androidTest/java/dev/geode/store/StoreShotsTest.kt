package dev.geode.store

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import dev.geode.journey.awaitEmptyPlayer
import dev.geode.journey.tap
import dev.geode.ui.MainActivity
import org.junit.After
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * The Play Store screenshot set: eight captures of the real app, a real style reacting to music
 * that is really playing, taken by the system compositor. Not a check and not a mock-up: a tool
 * that needs a device, like the thumbnail renderer and the UI tour it is built on.
 *
 * Skipped unless the instrumentation argument `storeshots=1` is set, so the ordinary emulator job
 * never pays for it. `.github/workflows/store-assets.yml` sets it and runs the class once per
 * screen (`storeForm=phone|phone-land|tablet7|tablet10`, sized there with `wm size`), which gives
 * Play's phone, 7-inch and 10-inch sets from one emulator. docs/store/screenshots.md says what
 * each capture is for and which Play rule sizes it.
 *
 * One test per capture: Orchestrator runs each on a fresh install, so a capture starts from a
 * known app (the style its [StoreShot] names, nothing left open by the capture before it) and a
 * crash costs one file. The set is the free app: every style here is one a free user keeps, and
 * where a list shows Pro badges, that is what a free user sees.
 *
 * Labels are the app's English strings, matched as the journeys match them.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class StoreShotsTest {
    private val setup = StoreSetup(gateArg = "storeshots")
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(setup).around(compose)

    private val stage by lazy { StoreStage(compose, setup) }

    @After
    fun release() = stage.release()

    /** The app's first style, alone on the screen: the picture the listing leads with. */
    @Test
    @StoreShot(index = 1, name = "visual-neon-chrome-sea")
    fun visualFirstStyle() = visualAlone()

    /** Styles: the menu of families over the running visual. */
    @Test
    @StoreShot(index = 2, name = "styles", styleId = NEBULA)
    fun styles() = visualsPage(STYLES)

    @Test
    @StoreShot(index = 3, name = "visual-nebula", styleId = NEBULA)
    fun visualNebula() = visualAlone()

    /** Customize: the categories a look is shaped with. */
    @Test
    @StoreShot(index = 4, name = "customize", styleId = TERRAIN)
    fun customize() = visualsPage(CUSTOMIZE)

    @Test
    @StoreShot(index = 5, name = "visual-terrain", styleId = TERRAIN)
    fun visualTerrain() = visualAlone()

    /** The music player, a track playing. */
    @Test
    @StoreShot(index = 6, name = "player")
    fun player() {
        compose.awaitEmptyPlayer()
        stage.playGroove()
        stage.warmUp()
        stage.shot()
    }

    /** Studio › Render: the song playing as the source of a video. */
    @Test
    @StoreShot(index = 7, name = "studio-render")
    fun studioRender() {
        compose.awaitEmptyPlayer()
        stage.playGroove()
        compose.tap(STUDIO)
        // The Render tab, not the Render button below the page that shares its name.
        runCatching { compose.tap(hasText(RENDER) and SemanticsMatcher.keyIsDefined(SemanticsProperties.Selected)) }
        stage.warmUp(PAGE_SETTLE_MS)
        stage.shot()
    }

    /** Presets: the saved and bundled looks. */
    @Test
    @StoreShot(index = 8, name = "presets", styleId = BULB)
    fun presets() = visualsPage(PRESETS)

    private fun visualAlone() {
        compose.awaitEmptyPlayer()
        stage.playGroove()
        stage.openVisuals()
        stage.visualAlone()
        stage.warmUp()
        stage.shot()
    }

    /** A page of the options over the visual; a tab raises the sheet to half (VisualsNavState). */
    private fun visualsPage(tab: String) {
        compose.awaitEmptyPlayer()
        stage.playGroove()
        stage.openVisuals()
        compose.tap(tab)
        stage.warmUp()
        stage.shot()
    }

    private companion object {
        const val STYLES = "Styles"
        const val CUSTOMIZE = "Customize"
        const val PRESETS = "Presets"
        const val STUDIO = "Studio"
        const val RENDER = "Render"

        // Family openers, the free style of each family (core/viz/Catalogue, pro/FreeTier).
        const val NEBULA = "nebula_dust"
        const val TERRAIN = "terra_ridge"
        const val BULB = "bulb_classic"

        const val PAGE_SETTLE_MS = 2_000L
    }
}
