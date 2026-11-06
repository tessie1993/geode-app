package dev.geode.journey

import android.app.UiAutomation
import android.content.Context
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import androidx.core.content.edit
import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import dev.geode.BuildConfig
import dev.geode.billing.CachedEntitlement
import dev.geode.billing.PrefsEntitlementStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.ThemeStore
import org.junit.Assume.assumeTrue
import org.junit.rules.TestRule
import org.junit.runner.Description
import org.junit.runners.model.Statement
import java.util.Locale

/**
 * The three looks a UI tour run takes, picked by the instrumentation argument `uitourConfig`
 * (`portrait`, `landscape` or `font`; portrait when absent). One run is one config, so
 * `.github/workflows/ui-tour.yml` runs the class three times on one emulator boot, and a config
 * that dies (an emulator hiccup, a crash only landscape provokes) costs only its own screenshots.
 *
 * The font config's scale comes from `uitourFontScale` (default [DEFAULT_LARGE_FONT]). 1.5 rather
 * than 2.0 by default: 1.3 is the largest the API 30 Settings app offers, so 1.5 already sits past
 * what most users pick, and at 2.0 nearly every row on a phone wraps, which makes a screenshot that
 * says "too big" about everything and therefore about nothing in particular.
 */
enum class TourConfig(
    val id: String,
    val landscape: Boolean,
) {
    PORTRAIT("portrait", false),
    LANDSCAPE("landscape", true),
    FONT("font", false),
    ;

    val fontScale: Float
        get() =
            if (this == FONT) {
                tourArgs().getString(FONT_ARG)?.toFloatOrNull() ?: DEFAULT_LARGE_FONT
            } else {
                1.0f
            }

    /** The orientation and font scale as they appear in every screenshot's name. */
    val orientationTag: String get() = if (landscape) "landscape" else "portrait"

    val fontTag: String get() = String.format(Locale.US, "%.1fx", fontScale)

    companion object {
        fun fromArgs(): TourConfig {
            val id = tourArgs().getString(CONFIG_ARG) ?: PORTRAIT.id
            return entries.firstOrNull { it.id == id } ?: PORTRAIT
        }
    }
}

/**
 * How one tour test seeds the app before the activity starts. On the test method rather than in
 * a rule per class, because Orchestrator gives each method a fresh install (clearPackageData) and
 * the cases want different first-run states: one meets the safety notice, one the tour pointers,
 * the rest come in past onboarding, some as a Pro owner.
 *
 * [index] numbers the case's screenshots (`index * 100` onward), so the files of every case sort
 * in tour order even though Orchestrator runs each case in its own process.
 */
@Target(AnnotationTarget.FUNCTION)
@Retention(AnnotationRetention.RUNTIME)
annotation class TourCase(
    val index: Int,
    val area: String,
    val onboarded: Boolean = true,
    val tutorialSeen: Boolean = true,
    val pro: Boolean = false,
)

/**
 * The outermost rule of [UiTourTest]: skips the case unless `-e uitour 1`, then puts the device
 * in the run's [TourConfig] and seeds the app's prefs from the method's [TourCase], all before the
 * compose rule inside it launches the activity.
 *
 * The skip has to happen here, ahead of the compose rule: an `assumeTrue` in `@Before` runs after
 * every rule has started, so the ordinary emulator suite (android.yml runs the whole package)
 * would still pay for one app launch per tour case before skipping it. Skipped here, a case costs
 * that suite only Orchestrator's empty process.
 *
 * Rotation and font scale are device settings, not app state, so they outlive clearPackageData;
 * they are put back (portrait, rotation unfrozen, font 1.0) after each case whatever happened in
 * it, or a landscape case would leave the next config's run lying on its side.
 */
class TourSetup : TestRule {
    val config: TourConfig = TourConfig.fromArgs()

    lateinit var case: TourCase
        private set

    override fun apply(
        base: Statement,
        description: Description,
    ): Statement =
        object : Statement() {
            override fun evaluate() {
                assumeTrue("the UI tour runs only with -e $TOUR_ARG 1", tourArgs().getString(TOUR_ARG) == "1")
                case = checkNotNull(description.getAnnotation(TourCase::class.java)) { "tour case without @TourCase" }
                val seeded =
                    object : Statement() {
                        override fun evaluate() {
                            seedBeyondOnboarding(case)
                            base.evaluate()
                        }
                    }
                applyDeviceConfig(config)
                try {
                    Onboarded(skipOnboarding = case.onboarded).apply(seeded, description).evaluate()
                } finally {
                    restoreDeviceConfig()
                }
            }
        }

    /**
     * What [Onboarded] does not cover: the tour pointers left unseen for the case that films them,
     * and Pro for the cases that need its pages. Pro is the cached entitlement FakeBillingRepository
     * reads at start ([PrefsEntitlementStore], the same file a fake purchase writes), so the app
     * comes up owning Pro with no purchase flow to drive. Builds billed against the real Play Store
     * (`-Pmuzviz.fakeBilling=false`) have nothing to fake; their Pro cases walk the free app.
     */
    private fun seedBeyondOnboarding(case: TourCase) {
        val context = ApplicationProvider.getApplicationContext<Context>()
        if (case.onboarded && !case.tutorialSeen) {
            context.getSharedPreferences(GeodePrefsFiles.GENERAL, Context.MODE_PRIVATE).edit(commit = true) {
                putBoolean(ThemeStore.KEY_TUTORIAL_SEEN, false)
            }
        }
        if (case.pro && BuildConfig.USE_FAKE_BILLING) {
            val store = PrefsEntitlementStore(PrefsEntitlementStore.open(context))
            store.save(CachedEntitlement(owned = true, verifiedAtMs = System.currentTimeMillis()))
        }
    }
}

/** Rotation first, then the font: both settle before the activity reads its configuration. */
private fun applyDeviceConfig(config: TourConfig) {
    TourDevice.shell("settings put system font_scale ${config.fontScale}")
    val rotation = if (config.landscape) UiAutomation.ROTATION_FREEZE_90 else UiAutomation.ROTATION_FREEZE_0
    TourDevice.automation.setRotation(rotation)
    SystemClock.sleep(CONFIG_SETTLE_MS)
}

private fun restoreDeviceConfig() {
    TourDevice.shell("settings put system font_scale 1.0")
    TourDevice.automation.setRotation(UiAutomation.ROTATION_FREEZE_0)
    TourDevice.automation.setRotation(UiAutomation.ROTATION_UNFREEZE)
}

/**
 * The device side of the tour: screenshots, shell commands and key presses, all through the
 * instrumentation's [UiAutomation]. UiAutomation rather than UI Automator's UiDevice because
 * UiDevice's screenshot, swipe and back are thin wrappers over these same calls, and using them
 * directly keeps the tour free of a new test dependency. The screenshot is the point: it is taken
 * by the system compositor, so it holds the GL SurfaceView's frame, which Compose's
 * `captureToImage` (a software draw of the view tree) leaves black.
 */
object TourDevice {
    val automation: UiAutomation get() = InstrumentationRegistry.getInstrumentation().uiAutomation

    /** Runs [command] as the shell user and returns what it printed; never throws. */
    fun shell(command: String): String =
        runCatching {
            ParcelFileDescriptor.AutoCloseInputStream(automation.executeShellCommand(command)).use {
                it.readBytes().toString(Charsets.UTF_8)
            }
        }.getOrElse { "shell failed: ${it.message}" }
}

internal fun tourArgs() = InstrumentationRegistry.getArguments()

private const val TOUR_ARG = "uitour"
private const val CONFIG_ARG = "uitourConfig"
private const val FONT_ARG = "uitourFontScale"
private const val DEFAULT_LARGE_FONT = 1.5f

/** A rotation or font change restarts nothing here, but the window needs a moment to relayout. */
private const val CONFIG_SETTLE_MS = 1_500L
