package dev.geode.cast

import kotlinx.coroutines.flow.MutableSharedFlow
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * The UI's flows are let go when the screen that fed them goes, and an old screen never stops a newer one's.
 * The flows never complete, so a job is cancelled rather than finished before [CastMirror] lets it go.
 */
@RunWith(RobolectricTestRunner::class)
class CastMirrorFollowTest {
    @After
    fun tearDown() {
        CastMirror.unfollow(CastMirror.follow(MutableSharedFlow()))
    }

    @Test
    fun unfollowStopsTheCollector() {
        val job = CastMirror.follow(MutableSharedFlow())
        CastMirror.unfollow(job)
        assertTrue(job.isCancelled)
    }

    @Test
    fun anOldScreenGoingLeavesTheNewOneFollowing() {
        val old = CastMirror.follow(MutableSharedFlow())
        val next = CastMirror.follow(MutableSharedFlow())
        assertTrue(old.isCancelled)
        CastMirror.unfollow(old)
        assertFalse(next.isCancelled)
        CastMirror.unfollow(next)
        assertTrue(next.isCancelled)
    }
}
