package dev.geode.store

import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import dev.geode.journey.awaitEmptyPlayer
import dev.geode.ui.MainActivity
import org.junit.After
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith

/**
 * Raw clips for the promo video: the screen recorded (`screenrecord`) while one free style runs
 * full screen on the demo groove. One clip per style rather than one long take that swipes
 * between styles, because a free user's swipe reaches Pro styles and the paywall with them, and
 * a clip per test keeps what the emulator did not lose.
 *
 * Skipped unless `storereel=1`; `storeReelSeconds` sets each clip's length (default
 * [DEFAULT_SECONDS]; `screenrecord` stops at 180). These are material, not the video: the CI
 * emulator renders in software and has no audio device, so a clip is silent and well below the
 * frame rate of a phone. docs/store/promo-video.md has the shot list and what to record on a
 * real phone instead.
 */
@LargeTest
@RunWith(AndroidJUnit4::class)
class StoreReelTest {
    private val setup = StoreSetup(gateArg = "storereel")
    private val compose = createAndroidComposeRule<MainActivity>()

    @get:Rule
    val rules: RuleChain = RuleChain.outerRule(setup).around(compose)

    private val stage by lazy { StoreStage(compose, setup) }

    @After
    fun release() = stage.release()

    @Test
    @StoreShot(index = 1, name = "neon-chrome-sea")
    fun neonChromeSea() = clip()

    @Test
    @StoreShot(index = 2, name = "nebula", styleId = "nebula_dust")
    fun nebula() = clip()

    @Test
    @StoreShot(index = 3, name = "terrain", styleId = "terra_ridge")
    fun terrain() = clip()

    @Test
    @StoreShot(index = 4, name = "bulb", styleId = "bulb_classic")
    fun bulb() = clip()

    private fun clip() {
        compose.awaitEmptyPlayer()
        stage.playGroove()
        stage.openVisuals()
        stage.visualAlone()
        val seconds =
            storeArgs()
                .getString(SECONDS_ARG)
                ?.toIntOrNull()
                ?.coerceIn(MIN_SECONDS, MAX_SECONDS)
                ?: DEFAULT_SECONDS
        stage.record(seconds)
    }

    private companion object {
        const val SECONDS_ARG = "storeReelSeconds"
        const val DEFAULT_SECONDS = 20
        const val MIN_SECONDS = 5
        const val MAX_SECONDS = 180
    }
}
