package dev.geode.ui

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * #322: My videos' delete-with-Undo must finish even when the tab (its composition scope) goes
 * away while the snackbar shows, and Undo must still keep the item while it is offered.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class PendingDeletesTest {
    /** Stands in for the project and take stores: what is still there. */
    private class FakeStore(
        vararg items: String,
    ) {
        val items = items.toMutableList()

        fun delete(item: String) {
            items.remove(item)
        }
    }

    private fun TestScope.pendingOver(store: FakeStore) =
        PendingDeletes<String>(backgroundScope, windowMs = 4_000L, keyOf = { it }, commit = store::delete)

    /** The tab's scope: it only shows the snackbar, and leaving the tab cancels it. */
    private fun TestScope.uiScope(): CoroutineScope = CoroutineScope(Job() + StandardTestDispatcher(testScheduler))

    @Test
    fun deleteCompletesAfterTheWindowEvenWhenTheUiLeaves() =
        runTest {
            val store = FakeStore("a", "b")
            val deletes = pendingOver(store)
            val ui = uiScope()

            deletes.delete("a")
            ui.launch { awaitCancellation() } // the snackbar, waiting
            runCurrent()
            assertEquals(setOf("a"), deletes.pending.value)
            assertEquals(listOf("a", "b"), store.items)

            ui.cancel() // the user leaves My videos mid-snackbar
            advanceTimeBy(4_001L)
            runCurrent()

            assertEquals(listOf("b"), store.items)
            assertTrue(deletes.pending.value.isEmpty())
        }

    @Test
    fun undoWhileOfferedKeepsTheItem() =
        runTest {
            val store = FakeStore("a")
            val deletes = pendingOver(store)

            deletes.delete("a")
            advanceTimeBy(2_000L)
            deletes.undo("a")
            advanceUntilIdle()

            assertEquals(listOf("a"), store.items)
            assertTrue(deletes.pending.value.isEmpty())
        }

    @Test
    fun nothingIsDeletedBeforeTheWindowEnds() =
        runTest {
            val store = FakeStore("a")
            val deletes = pendingOver(store)

            deletes.delete("a")
            advanceTimeBy(3_999L)
            runCurrent()

            assertEquals(listOf("a"), store.items)
            assertEquals(setOf("a"), deletes.pending.value)
        }

    @Test
    fun aSecondDeleteOfTheSameItemDoesNotRestartOrDoubleDelete() =
        runTest {
            val deleted = mutableListOf<String>()
            val deletes = PendingDeletes<String>(backgroundScope, windowMs = 4_000L, keyOf = { it }) { deleted += it }

            deletes.delete("a")
            advanceTimeBy(3_000L)
            deletes.delete("a")
            advanceTimeBy(1_001L)
            runCurrent()

            assertEquals(listOf("a"), deleted)
        }
}
