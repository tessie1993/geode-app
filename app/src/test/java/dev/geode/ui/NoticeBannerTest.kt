package dev.geode.ui

import android.os.Looper
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.compose.LocalLifecycleOwner
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import java.time.Duration

/** A notice raised while the app is away (the lock ending a capture) is still there when the person comes back. */
@RunWith(RobolectricTestRunner::class)
class NoticeBannerTest {
    @get:Rule
    val compose = createComposeRule()

    private class Owner : LifecycleOwner {
        val registry = LifecycleRegistry.createUnsafe(this)

        override val lifecycle: Lifecycle get() = registry
    }

    private fun pass(ms: Long) {
        compose.mainClock.advanceTimeBy(ms)
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(ms))
        compose.waitForIdle()
    }

    @Test
    fun itsTimeCountsOnlyWhileTheAppIsInFront() {
        val owner = Owner().apply { registry.currentState = Lifecycle.State.CREATED }
        var dismissed = 0
        compose.mainClock.autoAdvance = false
        compose.setContent {
            CompositionLocalProvider(LocalLifecycleOwner provides owner) {
                MaterialTheme {
                    NoticeBanner("Stopped", action = "Resume", actionDescription = null, onAction = {}, onDismiss = { dismissed++ })
                }
            }
        }
        pass(NOTICE_VISIBLE_MS * 2)
        assertEquals(0, dismissed)

        compose.runOnIdle { owner.registry.currentState = Lifecycle.State.RESUMED }
        pass(NOTICE_VISIBLE_MS / 2)
        assertEquals(0, dismissed)
        pass(NOTICE_VISIBLE_MS)
        assertEquals(1, dismissed)
    }
}
