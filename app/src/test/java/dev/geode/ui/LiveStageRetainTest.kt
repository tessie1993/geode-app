package dev.geode.ui

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner

/**
 * The live stage's owner: a dark-mode schedule, a font or display size change or a keyboard recreates the
 * Activity, and the stage a recording and a cast draw from must come through it; only finishing ends it.
 */
@RunWith(RobolectricTestRunner::class)
class LiveStageRetainTest {
    class Token

    /** Holds its value the way AppShell holds the live stage, composed from onCreate like MainActivity. */
    class Host : ComponentActivity() {
        override fun onCreate(savedInstanceState: Bundle?) {
            super.onCreate(savedInstanceState)
            setContent {
                seen += retainUntilRetired(open = { Token().also { opened += it } }, close = { closed += it })
            }
        }
    }

    @Before
    fun fresh() {
        opened.clear()
        closed.clear()
        seen.clear()
    }

    @Test
    fun aRecreationKeepsTheStageOpen() {
        val host = Robolectric.buildActivity(Host::class.java).setup()
        host.recreate()

        assertEquals(1, opened.size)
        assertEquals(emptyList<Token>(), closed)
        assertSame(opened.single(), seen.last())
    }

    @Test
    fun finishingClosesItOnce() {
        val host = Robolectric.buildActivity(Host::class.java).setup()
        host.recreate()
        host.get().finish()
        host.pause().stop().destroy()

        assertEquals(1, closed.size)
        assertEquals(opened, closed)
    }

    private companion object {
        val opened = mutableListOf<Token>()
        val closed = mutableListOf<Token>()
        val seen = mutableListOf<Token>()
    }
}
