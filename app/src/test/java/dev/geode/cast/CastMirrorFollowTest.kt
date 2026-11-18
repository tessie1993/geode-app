package dev.geode.cast

import kotlinx.coroutines.flow.emptyFlow
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** The UI's flows are let go when the screen that fed them goes, and an old screen never stops a newer one's. */
@RunWith(RobolectricTestRunner::class)
class CastMirrorFollowTest {
    @After
    fun tearDown() {
        CastMirror.unfollow(CastMirror.follow(emptyFlow()))
    }

    @Test
    fun unfollowStopsTheCollector() {
        val job = CastMirror.follow(emptyFlow())
        CastMirror.unfollow(job)
        assertTrue(job.isCancelled)
    }

    @Test
    fun anOldScreenGoingLeavesTheNewOneFollowing() {
        val old = CastMirror.follow(emptyFlow())
        val next = CastMirror.follow(emptyFlow())
        assertTrue(old.isCancelled)
        CastMirror.unfollow(old)
        assertFalse(next.isCancelled)
        CastMirror.unfollow(next)
        assertTrue(next.isCancelled)
    }
}
