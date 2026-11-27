package dev.geode.ui

import android.app.Activity
import android.app.Application
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.retain.RetainedEffect
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.test.core.app.ApplicationProvider
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.android.controller.ActivityController

/**
 * The live stage's owner: a dark-mode schedule, a font or display size change or a keyboard recreates the
 * Activity, and the stage a recording and a cast draw from must come through it; only the Activity's end closes it.
 */
@RunWith(RobolectricTestRunner::class)
class LiveStageRetainTest {
    class Token

    class Log {
        val opened = mutableListOf<Token>()
        val closed = mutableListOf<Token>()
        val seen = mutableListOf<Token>()
        var retired = 0
        var cleared = 0
    }

    class Probe : ViewModel()

    /** Holds its value the way AppShell holds the live stage, composed from onCreate like MainActivity. */
    class Host : ComponentActivity() {
        lateinit var log: Log

        override fun onCreate(savedInstanceState: Bundle?) {
            super.onCreate(savedInstanceState)
            val log = log
            setContent {
                log.seen += retainUntilRetired(open = { Token().also { log.opened += it } }, close = { log.closed += it })
                RetainedEffect(Unit) { onRetire { log.retired++ } }
            }
        }
    }

    private val log = Log()
    private val app = ApplicationProvider.getApplicationContext<Application>()

    // Every Host, a recreated one too, writes to this test's log.
    private val handLog =
        object : Application.ActivityLifecycleCallbacks {
            override fun onActivityPreCreated(
                activity: Activity,
                savedInstanceState: Bundle?,
            ) {
                (activity as? Host)?.log = log
            }

            override fun onActivityCreated(
                activity: Activity,
                savedInstanceState: Bundle?,
            ) = Unit

            override fun onActivityStarted(activity: Activity) = Unit

            override fun onActivityResumed(activity: Activity) = Unit

            override fun onActivityPaused(activity: Activity) = Unit

            override fun onActivityStopped(activity: Activity) = Unit

            override fun onActivitySaveInstanceState(
                activity: Activity,
                outState: Bundle,
            ) = Unit

            override fun onActivityDestroyed(activity: Activity) = Unit
        }

    @Before
    fun listen() = app.registerActivityLifecycleCallbacks(handLog)

    @After
    fun stopListening() = app.unregisterActivityLifecycleCallbacks(handLog)

    private fun host(): ActivityController<Host> = Robolectric.buildActivity(Host::class.java).setup()

    @Test
    fun aRecreationKeepsTheStageOpen() {
        val host = host()
        host.recreate()

        assertEquals(1, log.opened.size)
        assertEquals(emptyList<Token>(), log.closed)
        assertSame(log.opened.single(), log.seen.last())
        assertEquals(0, log.retired)
    }

    @Test
    fun finishingClosesItOnce() {
        val host = host()
        host.recreate()
        host.get().finish()
        host.pause().stop().destroy()

        assertEquals(1, log.closed.size)
        assertEquals(log.opened, log.closed)
        assertEquals(1, log.retired)
    }

    @Test
    fun theSystemDestroyingItWithoutFinishingClosesItOnce() {
        val host = host()
        ViewModelProvider(host.get())[Probe::class.java].addCloseable { log.cleared++ }
        host.pause().stop().destroy()

        assertEquals(1, log.cleared)
        assertEquals(1, log.closed.size)
        assertEquals(log.opened, log.closed)
        assertEquals(1, log.retired)
    }

    @Test
    fun aRecreationWhileStoppedThenFinishingClosesItOnce() {
        val host = host()
        host.pause().stop()
        host.recreate()
        assertEquals(Lifecycle.State.CREATED, host.get().lifecycle.currentState)
        host.get().finish()
        host.destroy()

        assertEquals(1, log.opened.size)
        assertEquals(log.opened, log.closed)
        assertEquals(1, log.retired)
    }
}
