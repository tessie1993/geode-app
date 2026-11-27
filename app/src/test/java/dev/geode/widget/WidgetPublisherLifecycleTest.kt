package dev.geode.widget

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.withContext
import org.junit.Assert.assertEquals
import org.junit.Test

class WidgetPublisherLifecycleTest {
    @Test
    fun `stopped write follows cancelled publisher work`() =
        runTest {
            val release = CompletableDeferred<Unit>()
            val events = mutableListOf<String>()
            val publish =
                launch(start = CoroutineStart.UNDISPATCHED) {
                    try {
                        withContext(NonCancellable) { release.await() }
                        events += "published"
                    } finally {
                        events += "finished"
                    }
                }
            val clear =
                async(start = CoroutineStart.UNDISPATCHED) {
                    WidgetPublisher.afterCancelled(listOf(publish)) { events += "cleared" }
                }

            runCurrent()
            assertEquals(emptyList<String>(), events)

            release.complete(Unit)
            runCurrent()
            clear.await()

            assertEquals(listOf("published", "finished", "cleared"), events)
        }
}
