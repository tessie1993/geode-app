package dev.geode.store

import android.content.Context
import android.graphics.Bitmap
import android.os.SystemClock
import android.util.Log
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.performClick
import androidx.core.content.edit
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.media3.session.MediaBrowser
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.services.storage.TestStorage
import dev.geode.BuildConfig
import dev.geode.billing.CachedEntitlement
import dev.geode.billing.PrefsEntitlementStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.PerformanceStore
import dev.geode.data.Preset
import dev.geode.data.PresetStore
import dev.geode.data.VizUiState
import dev.geode.journey.LIVE_VISUAL
import dev.geode.journey.Onboarded
import dev.geode.journey.TourDevice
import dev.geode.journey.VISUALS
import dev.geode.journey.awaitMain
import dev.geode.journey.awaitNode
import dev.geode.journey.connectBrowser
import dev.geode.journey.hasCustomAction
import dev.geode.journey.onMain
import dev.geode.journey.stopAndRelease
import dev.geode.journey.tap
import dev.geode.pro.FreeTier
import dev.geode.render.PerformanceMode
import dev.geode.ui.MainActivity
import org.junit.Assume.assumeTrue
import org.junit.rules.TestRule
import org.junit.runner.Description
import org.junit.runners.model.Statement
import java.util.Locale

/** The compose rule every store capture runs under. */
internal typealias StoreRule = AndroidComposeTestRule<ActivityScenarioRule<MainActivity>, MainActivity>

/**
 * One store capture: its number in the set, its file name, and the style on screen when the app
 * comes up. In a free capture the style has to be one a free user keeps ([FreeTier.freeStyleIds]),
 * and [StoreSetup] refuses any other: a locked style would be swapped for the first style at
 * start, and the capture would show something other than its name says.
 */
@Target(AnnotationTarget.FUNCTION)
@Retention(AnnotationRetention.RUNTIME)
internal annotation class StoreShot(
    val index: Int,
    val name: String,
    val styleId: String = FreeTier.FIRST_STYLE_ID,
)

/**
 * The outermost rule of the store captures (docs/store/screenshots.md): skips the case unless the
 * class's own instrumentation argument is `1`, then seeds the app before the activity starts.
 *
 * Like the UI tour's rule, the skip has to sit ahead of the compose rule, or the ordinary emulator
 * suite (android.yml runs every package) would launch the app once per capture before skipping it.
 *
 * What is seeded, all of it the app's own stores so the captures show the real app:
 * - past onboarding, through [Onboarded] (which also hands the prefs to the live repository);
 * - Settings › Performance, from `storeQuality` (`high`, `balanced` or `saver`). [Onboarded] pins
 *   Saver, half render scale, which is right for a journey and too soft for a store image; the
 *   workflow asks for High first and falls back when the software-rendered emulator cannot keep
 *   the app alive at that cost;
 * - the style on screen ([StoreShot.styleId]), as the saved live look the session restores;
 * - Pro, only for a class that asks for it ([pro]), on a build billed against the fake store (the
 *   case is skipped on any other build). The owner's rule (2026-11-11): the screenshots show what
 *   a free user gets, the recorded clips show Pro. So [StoreShotsTest] is the free app and
 *   [StoreReelTest] owns Pro; no run argument can turn a screenshot into a Pro one.
 *
 * The screen size is not set here: the workflow sets it with `wm size` and `wm density` before the
 * run and names it in `storeForm`, which is only the folder the files go in.
 */
internal class StoreSetup(
    private val gateArg: String,
    private val pro: Boolean = false,
) : TestRule {
    val form: String = storeArgs().getString(FORM_ARG) ?: DEFAULT_FORM

    lateinit var shot: StoreShot
        private set

    override fun apply(
        base: Statement,
        description: Description,
    ): Statement =
        object : Statement() {
            override fun evaluate() {
                assumeTrue("store captures run only with -e $gateArg 1", storeArgs().getString(gateArg) == "1")
                assumeTrue("a Pro capture needs a build billed against the fake store", !pro || BuildConfig.USE_FAKE_BILLING)
                shot = checkNotNull(description.getAnnotation(StoreShot::class.java)) { "store capture without @StoreShot" }
                check(pro || shot.styleId in FreeTier.freeStyleIds) { "${shot.styleId} is a Pro style in a free capture" }
                val seeded =
                    object : Statement() {
                        override fun evaluate() {
                            seed(shot)
                            base.evaluate()
                        }
                    }
                Onboarded().apply(seeded, description).evaluate()
            }
        }

    private fun seed(shot: StoreShot) {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val general = context.getSharedPreferences(GeodePrefsFiles.GENERAL, Context.MODE_PRIVATE)
        PerformanceStore(general, context).set(qualityFromArgs().settings)
        val fresh = VizUiState()
        val look = Preset(LIVE_LOOK_NAME, shot.styleId, fresh.attack, fresh.decay, null, fresh.params)
        context.getSharedPreferences(GeodePrefsFiles.VIZ, Context.MODE_PRIVATE).edit(commit = true) {
            putString(LIVE_STATE_KEY, PresetStore.toJson(look))
        }
        if (pro) {
            val store = PrefsEntitlementStore(PrefsEntitlementStore.open(context))
            store.save(CachedEntitlement(owned = true, verifiedAtMs = System.currentTimeMillis()))
        }
    }

    private fun qualityFromArgs(): PerformanceMode {
        val asked = storeArgs().getString(QUALITY_ARG)?.uppercase(Locale.US)
        return PerformanceMode.entries.firstOrNull { it.name == asked } ?: PerformanceMode.HIGH
    }

    private companion object {
        const val FORM_ARG = "storeForm"
        const val QUALITY_ARG = "storeQuality"
        const val DEFAULT_FORM = "phone"

        // VizStateStore's key and the name it saves the live look under; both private there.
        const val LIVE_STATE_KEY = "live_state"
        const val LIVE_LOOK_NAME = "__live__"
    }
}

/**
 * What a capture does with the running app: plays the demo groove, opens Visuals, clears the
 * options and the chrome off the visual, and writes the screenshot or the screen recording.
 *
 * Screenshots are the system compositor's (UiAutomation), so they hold the GL surface's frame,
 * and they are written without an alpha channel: Play takes "JPEG or 24-bit PNG (no alpha)"
 * (support.google.com/googleplay/android-developer/answer/9866151). Files go through
 * [TestStorage] under `store-shots/<form>/`, which survives Orchestrator's clearPackageData.
 */
internal class StoreStage(
    private val compose: StoreRule,
    private val setup: StoreSetup,
) {
    private val storage = TestStorage()
    private var browser: MediaBrowser? = null

    /** The groove through PlaybackService, on repeat, as a track a user had picked would play. */
    fun playGroove() {
        val b = browser ?: connectBrowser().also { browser = it }
        val uri = DemoGroove.write(ApplicationProvider.getApplicationContext())
        val item =
            MediaItem
                .Builder()
                .setMediaId(uri.toString())
                .setUri(uri)
                .setMediaMetadata(MediaMetadata.Builder().setTitle(DemoGroove.TITLE).build())
                .build()
        onMain {
            b.repeatMode = Player.REPEAT_MODE_ONE
            b.setMediaItem(item)
            b.prepare()
            b.play()
        }
        awaitMain("the groove to play") { b.isPlaying }
    }

    fun openVisuals() {
        compose.tap(VISUALS)
        compose.awaitNode(hasContentDescription(LIVE_VISUAL))
    }

    /** The visual alone: the options hidden by their × (or its TalkBack action), the chrome left to fade. */
    fun visualAlone() {
        val close = hasContentDescription(HIDE_OPTIONS) and hasClickAction()
        if (compose.onAllNodes(close).fetchSemanticsNodes().isNotEmpty()) {
            compose.onAllNodes(close).onFirst().performClick()
        } else {
            customAction(HIDE_OPTIONS)
        }
        SystemClock.sleep(CHROME_FADE_MS)
    }

    /** Lets the visual run on the music before a capture, so it is mid-groove and not at its rest pose. */
    fun warmUp(ms: Long = WARM_UP_MS) = SystemClock.sleep(ms)

    fun shot() {
        runCatching { compose.waitForIdle() }
        val raw = checkNotNull(TourDevice.automation.takeScreenshot()) { "the system gave no screenshot" }
        val opaque = raw.copy(Bitmap.Config.ARGB_8888, true)
        opaque.setHasAlpha(false)
        val file = String.format(Locale.US, "store-shots/%s/%02d-%s.png", setup.form, setup.shot.index, setup.shot.name)
        storage.openOutputFile(file).use { check(opaque.compress(Bitmap.CompressFormat.PNG, PNG_QUALITY, it)) { "PNG encode failed" } }
        Log.i(TAG, "$file ${opaque.width}x${opaque.height}")
        raw.recycle()
        opaque.recycle()
    }

    /**
     * Records the screen for [seconds] with the device's own `screenrecord`, as the shell user;
     * returns once the file is closed. The clip lands in shared storage, which the shell can write
     * and the workflow pulls; TestStorage has no way to hand a path to a shell command.
     */
    fun record(seconds: Int) {
        val dir = "$REEL_DIR/${setup.form}"
        val file = String.format(Locale.US, "%s/%02d-%s.mp4", dir, setup.shot.index, setup.shot.name)
        TourDevice.shell("mkdir -p $dir")
        val said = TourDevice.shell("screenrecord --time-limit $seconds --bit-rate $REEL_BIT_RATE $file")
        Log.i(TAG, "$file recorded ${said.trim()}")
        SystemClock.sleep(REEL_CLOSE_MS)
    }

    /** Stops the player and closes the activity before the compose rule cancels its Recomposer (see UiTourTest.release). */
    fun release() {
        browser?.let { b -> runCatching { b.stopAndRelease() } }
        compose.activityRule.scenario.close()
        InstrumentationRegistry.getInstrumentation().waitForIdleSync()
    }

    private fun customAction(label: String): Boolean {
        val node = compose.onAllNodes(hasCustomAction(label)).fetchSemanticsNodes().firstOrNull() ?: return false
        val action = node.config.getOrNull(SemanticsActions.CustomActions)?.firstOrNull { it.label == label } ?: return false
        return compose.runOnUiThread { action.action() }
    }

    private companion object {
        const val TAG = "StoreShots"
        const val HIDE_OPTIONS = "Hide the options"
        const val PNG_QUALITY = 100
        const val REEL_DIR = "/sdcard/store-assets/promo"
        const val REEL_BIT_RATE = 16_000_000

        /** The mini player and stage icons fade 6 s after the last touch (GeneralSettings.miniPlayerFadeSeconds). */
        const val CHROME_FADE_MS = 9_000L
        const val WARM_UP_MS = 6_000L

        /** screenrecord writes the MP4's index after its time is up. */
        const val REEL_CLOSE_MS = 2_000L
    }
}

internal fun storeArgs() = InstrumentationRegistry.getArguments()
