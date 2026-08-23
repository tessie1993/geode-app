package dev.geode

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Proves the instrumentation runner is wired and that the debug variant really
 * does install under its own id, which is what lets a debug build sit beside a
 * release one on the same device.
 */
@RunWith(AndroidJUnit4::class)
class BuildVariantTest {
    @Test
    fun debugBuildInstallsUnderItsOwnApplicationId() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertEquals("dev.geode.debug", context.packageName)
    }

    @Test
    fun theFileProviderAuthorityFollowsTheApplicationId() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val authority = "${context.packageName}.presets"
        val resolved =
            context.packageManager.resolveContentProvider(authority, 0)
        assertTrue("no FileProvider registered for $authority", resolved != null)
    }
}
