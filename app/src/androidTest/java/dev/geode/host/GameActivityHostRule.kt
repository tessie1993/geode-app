package dev.geode.host

import android.app.Activity
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.rules.ActivityScenarioRule
import org.junit.rules.Description
import org.junit.rules.Statement
import org.junit.rules.TestRule
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Owns one ActivityScenario, its optional Compose rule, probes, and device-setting checkpoint.
 *
 * Use [compose] for tests that need Compose semantics and [scenario] for lifecycle/window tests that
 * drive ActivityScenario directly. The inner AndroidX rule performs the launch; this outer rule
 * closes the scenario and every probe after `@After`, then restores shell settings. Cleanup failures
 * are attached to a test failure rather than replacing it.
 *
 * The Activity class is deliberately supplied by the test. This file does not depend on MainActivity
 * or on the not-yet-built StageView controller API.
 */
abstract class GameActivityHostRule<A : Activity> : TestRule {
    /** The device shell used by this test. It restores every setting changed through its set APIs. */
    val shell: DeviceShell = DeviceShell()

    /** The scenario owned by the selected AndroidX rule; valid after the inner rule has launched. */
    abstract val scenario: ActivityScenario<A>

    /** A fresh surface observer. Bind it before moving the surface into a Presentation. */
    fun stageProbe(): StageProbe = own(StageProbe(scenario))

    /** A fresh activity-window observer. It reads through the scenario, so recreation is safe. */
    fun windowProbe(): WindowProbe = WindowProbe(scenario)

    final override fun apply(
        base: Statement,
        description: Description,
    ): Statement =
        object : Statement() {
            override fun evaluate() {
                var failure: Throwable? = null
                try {
                    launchRule().apply(base, description).evaluate()
                } catch (t: Throwable) {
                    failure = t
                }

                val cleanupFailures = mutableListOf<Throwable>()
                closeResources(cleanupFailures)
                rethrowWithCleanup(failure, cleanupFailures)
            }
        }

    /** The AndroidX rule that launches and normally closes the Activity. */
    protected abstract fun launchRule(): TestRule

    private fun <C : AutoCloseable> own(closeable: C): C {
        closeables += closeable
        return closeable
    }

    private fun closeResources(failures: MutableList<Throwable>) {
        closeables.asReversed().forEach { closeable ->
            try {
                closeable.close()
            } catch (t: Throwable) {
                failures += t
            }
        }
        closeables.clear()

        try {
            scenario.close()
        } catch (t: Throwable) {
            // The inner AndroidX rule already closes it. This second close is deliberate: it also
            // covers a launch failure path and makes this rule's ownership explicit and idempotent.
            failures += t
        }

        try {
            shell.close()
        } catch (t: Throwable) {
            failures += t
        }
    }

    private fun rethrowWithCleanup(
        failure: Throwable?,
        cleanupFailures: List<Throwable>,
    ) {
        if (failure != null) {
            cleanupFailures.forEach(failure::addSuppressed)
            throw failure
        }
        if (cleanupFailures.isNotEmpty()) {
            val cleanup = AssertionError("GameActivity host teardown failed")
            cleanupFailures.forEach(cleanup::addSuppressed)
            throw cleanup
        }
    }

    private val closeables = mutableListOf<AutoCloseable>()

    companion object {
        /** Launches [A] and exposes both ActivityScenario and Compose testing APIs. */
        inline fun <reified A : Activity> compose(): ComposeGameActivityHostRule<A> =
            ComposeGameActivityHostRule(createAndroidComposeRule<A>())

        /** Launches [A] without a Compose rule, for lifecycle and platform-window tests. */
        fun <A : Activity> scenario(activityClass: Class<A>): ScenarioGameActivityHostRule<A> =
            ScenarioGameActivityHostRule(ActivityScenarioRule(activityClass))
    }
}

/** [GameActivityHostRule] variant for tests that interact with Compose semantics. */
class ComposeGameActivityHostRule<A : Activity> internal constructor(
    val compose: AndroidComposeTestRule<A>,
) : GameActivityHostRule<A>() {
    override val scenario: ActivityScenario<A>
        get() = compose.activityRule.scenario

    override fun launchRule(): TestRule = compose
}

/** [GameActivityHostRule] variant for tests that only need ActivityScenario. */
class ScenarioGameActivityHostRule<A : Activity> internal constructor(
    private val activityRule: ActivityScenarioRule<A>,
) : GameActivityHostRule<A>() {
    override val scenario: ActivityScenario<A>
        get() = activityRule.scenario

    override fun launchRule(): TestRule = activityRule
}

/** Short factory for the common Compose-backed host test. */
inline fun <reified A : Activity> gameActivityHostRule(): ComposeGameActivityHostRule<A> =
    GameActivityHostRule.compose()

/** Short factory for a lifecycle/window test that does not need Compose semantics. */
fun <A : Activity> gameActivityScenarioRule(activityClass: Class<A>): ScenarioGameActivityHostRule<A> =
    GameActivityHostRule.scenario(activityClass)

/**
 * Condition polling without Thread.sleep. The condition runs on the instrumentation thread; a main
 * looper task only wakes it at the polling cadence. A timeout is a failure, never a best-effort pass.
 */
internal fun <T> awaitHostProbe(
    description: String,
    timeoutMs: Long = HOST_PROBE_TIMEOUT_MS,
    diagnostics: () -> String = { "" },
    condition: () -> T?,
): T {
    require(timeoutMs > 0) { "Probe timeout must be positive" }
    val main = Handler(Looper.getMainLooper())
    val deadline = SystemClock.uptimeMillis() + timeoutMs

    while (true) {
        condition()?.let { return it }
        val remaining = deadline - SystemClock.uptimeMillis()
        if (remaining <= 0) {
            val detail =
                runCatching { diagnostics() }
                    .getOrNull()
                    ?.takeIf(String::isNotBlank)
                    ?.let { " Last state: $it" }
                    .orEmpty()
            throw AssertionError("Timed out after $timeoutMs ms waiting for $description.$detail")
        }

        val tick = CountDownLatch(1)
        main.postDelayed({ tick.countDown() }, minOf(HOST_PROBE_POLL_MS, remaining))
        tick.await(minOf(HOST_PROBE_POLL_MS, remaining) + HOST_PROBE_WAKE_SLOP_MS, TimeUnit.MILLISECONDS)
    }
}

private const val HOST_PROBE_TIMEOUT_MS = 20_000L
private const val HOST_PROBE_POLL_MS = 50L
private const val HOST_PROBE_WAKE_SLOP_MS = 250L
