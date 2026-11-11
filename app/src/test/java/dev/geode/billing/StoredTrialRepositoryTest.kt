package dev.geode.billing

import dev.geode.pro.FreeTier
import dev.geode.pro.TrialState
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The trial against a wall clock that does not follow the coroutine's own time: a phone asleep
 * (the wall clock runs, `delay` does not) and a clock set back by hand.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class StoredTrialRepositoryTest {
    private class Store : TrialStore {
        var startedAt: Long? = null
        var noticeShown = false
        var lastSeen = 0L

        override fun startedAtMs(): Long? = startedAt

        override fun endNoticeShown(): Boolean = noticeShown

        override fun lastSeenMs(): Long = lastSeen

        override fun saveStartedAt(ms: Long) {
            startedAt = ms
        }

        override fun saveEndNoticeShown() {
            noticeShown = true
        }

        override fun saveLastSeen(ms: Long) {
            lastSeen = ms
        }
    }

    private class Fixture(
        scope: TestScope,
    ) {
        var wall = START
        val store = Store()
        val clock = WallClock { wall }
        val repository = StoredTrialRepository(store, clock)
        val seen = mutableListOf<TrialState>()

        init {
            scope.backgroundScope.launch(UnconfinedTestDispatcher(scope.testScheduler)) {
                repository.getTrialStream().collect { seen += it }
            }
        }
    }

    @Test
    fun `a phone that slept through the hour loses the trial at the next re-read`() =
        runTest {
            val f = Fixture(this)
            f.repository.startTrial()
            runCurrent()
            assertEquals(TrialState.Active(START + HOUR), f.seen.last())

            f.wall += HOUR * 3
            advanceTimeBy(MINUTE + 1)

            assertEquals(TrialState.Ended(endNoticeShown = false), f.seen.last())
        }

    @Test
    fun `the trial still ends on the hour while the phone is awake`() =
        runTest {
            val f = Fixture(this)
            f.repository.startTrial()
            runCurrent()

            repeat(59) {
                f.wall += MINUTE
                advanceTimeBy(MINUTE)
            }
            runCurrent()
            assertTrue(f.seen.last() is TrialState.Active)

            f.wall += MINUTE
            advanceTimeBy(MINUTE)
            runCurrent()
            assertEquals(listOf(TrialState.Available, TrialState.Active(START + HOUR), TrialState.Ended(false)), f.seen)
        }

    @Test
    fun `a clock set back mid-trial ends it, and it stays ended in a new process`() =
        runTest {
            val f = Fixture(this)
            f.repository.startTrial()
            runCurrent()
            f.wall += MINUTE * 50
            advanceTimeBy(MINUTE + 1)
            assertTrue(f.seen.last() is TrialState.Active)

            f.wall = START + MINUTE * 10
            advanceTimeBy(MINUTE + 1)
            assertEquals(TrialState.Ended(endNoticeShown = false), f.seen.last())

            val restarted = StoredTrialRepository(f.store, f.clock)
            assertEquals(TrialState.Ended(endNoticeShown = false), restarted.getTrialStream().first())
            f.wall = START + HOUR - MINUTE
            assertEquals(TrialState.Ended(endNoticeShown = false), StoredTrialRepository(f.store, f.clock).getTrialStream().first())
        }

    private companion object {
        const val START = 1_000_000L
        const val MINUTE = 60_000L
        const val HOUR = FreeTier.TRIAL_DURATION_MS
    }
}
