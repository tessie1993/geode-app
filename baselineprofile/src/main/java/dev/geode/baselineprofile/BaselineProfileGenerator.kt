package dev.geode.baselineprofile

import android.os.SystemClock
import androidx.benchmark.macro.MacrobenchmarkScope
import androidx.benchmark.macro.junit4.BaselineProfileRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiObject2
import androidx.test.uiautomator.Until
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** Records the launch to the shell, and the walk on to the Visuals tab. Run by baseline-profile.yml. */
@RunWith(AndroidJUnit4::class)
class BaselineProfileGenerator {
    @get:Rule
    val rule = BaselineProfileRule()

    @Test
    fun startup() =
        rule.collect(packageName = targetPackage(), includeInStartupProfile = true) {
            killProcess()
            startActivityAndWait()
            passFirstRun()
            awaitShell()
        }

    @Test
    fun visuals() =
        rule.collect(packageName = targetPackage()) {
            killProcess()
            startActivityAndWait()
            passFirstRun()
            awaitShell().click()
            // nav_visuals
            check(device.wait(Until.hasObject(By.text("Visuals").selected(true)), STEP_WAIT_MS)) { "Visuals was not selected" }
            SystemClock.sleep(VISUALS_SETTLE_MS)
        }
}

// Release and sideload install under different application ids; the plugin passes the one under test.
private fun targetPackage(): String =
    requireNotNull(InstrumentationRegistry.getArguments().getString("androidx.benchmark.targetPackageName")) {
        "androidx.benchmark.targetPackageName is not set: run :app:generateBaselineProfile"
    }

/**
 * The boot intro, then the order AppLaunchSmokeTest walks: safety notice, tour, Pro offer. Data
 * persists between iterations, so only the first one meets those; later ones see the shell and return.
 */
private fun MacrobenchmarkScope.passFirstRun() {
    // The intro plays on every launch, over a shell whose tabs are already findable; it is the only TextureView.
    check(device.wait(Until.gone(By.clazz("android.view.TextureView")), SHELL_WAIT_MS)) { "The boot intro did not end" }
    val deadline = SystemClock.uptimeMillis() + SHELL_WAIT_MS
    while (SystemClock.uptimeMillis() < deadline) {
        val notice = device.findObject(By.text("I understand")) // safety_acknowledge
        if (notice != null) {
            notice.click()
            break
        }
        if (findTab() != null) return
        SystemClock.sleep(POLL_MS)
    }
    device.wait(Until.findObject(By.text("Skip tour")), STEP_WAIT_MS)?.click() // tour_skip_tour
    check(device.wait(Until.gone(By.text("Skip tour")), STEP_WAIT_MS)) { "The tour stayed up" } // tour_skip_tour
    if (device.wait(Until.hasObject(By.text("Make your music come to life")), STEP_WAIT_MS)) { // pro_title_onboarding
        (device.findObject(By.desc("Close")) ?: device.findObject(By.text("Close")))?.click() // pro_close
        // pro_title_onboarding
        check(device.wait(Until.gone(By.text("Make your music come to life")), STEP_WAIT_MS)) { "The Pro offer stayed up" }
    }
}

private fun MacrobenchmarkScope.awaitShell(): UiObject2 {
    val deadline = SystemClock.uptimeMillis() + SHELL_WAIT_MS
    while (SystemClock.uptimeMillis() < deadline) {
        findTab()?.let { return it }
        SystemClock.sleep(POLL_MS)
    }
    error("The Visuals tab did not show within $SHELL_WAIT_MS ms")
}

/** The selected tab draws its label; an unselected one in a narrow window carries it as a description. */
private fun MacrobenchmarkScope.findTab(): UiObject2? =
    device.findObject(By.text("Visuals").clickable(true)) // nav_visuals
        ?: device.findObject(By.desc("Visuals").clickable(true)) // nav_visuals

// The software-GL emulator stalls the main thread on each new GL surface.
private const val SHELL_WAIT_MS = 30_000L

// One first-run screen giving way to the next.
private const val STEP_WAIT_MS = 10_000L

// Often enough to catch the shell soon after it draws.
private const val POLL_MS = 250L

// Lets the visualizer draw its first frames, so their code is in the profile.
private const val VISUALS_SETTLE_MS = 3_000L
