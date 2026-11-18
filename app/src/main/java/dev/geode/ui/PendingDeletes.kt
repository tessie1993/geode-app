package dev.geode.ui

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * Deletes that wait out an Undo window before they happen (Studio › My videos).
 *
 * The wait runs in [scope], which outlives the screen: My videos used to wait inside its own
 * composition, so leaving the tab while the snackbar showed cancelled the wait and the deleted
 * item came back (#322). Here the screen only shows the snackbar and calls [undo]; the delete is
 * committed after [windowMs] whether or not anything is still on screen, and [commitAll] commits
 * what still waits when [scope]'s owner goes away first.
 *
 * Pure Kotlin on purpose, so a JVM test can drive it with a test scope and a fake store.
 */
class PendingDeletes<T>(
    private val scope: CoroutineScope,
    private val windowMs: Long = UNDO_WINDOW_MS,
    private val keyOf: (T) -> String,
    private val commit: (T) -> Unit,
) {
    private class Waiting<T>(
        val item: T,
        val job: Job,
    )

    private val waiting = mutableMapOf<String, Waiting<T>>()
    private val _pending = MutableStateFlow<Set<String>>(emptySet())

    /** Keys waiting out their window: hidden from the list as if already gone. */
    val pending: StateFlow<Set<String>> = _pending.asStateFlow()

    /** Hides [item] now and deletes it after the window unless [undo] comes first. */
    fun delete(item: T) {
        val key = keyOf(item)
        if (key in waiting) return
        _pending.update { it + key }
        val job =
            scope.launch {
                delay(windowMs)
                waiting.remove(key)
                _pending.update { it - key }
                commit(item)
            }
        waiting[key] = Waiting(item, job)
    }

    /** Keeps the item: its delete never happens. */
    fun undo(key: String) {
        waiting.remove(key)?.job?.cancel() ?: return
        _pending.update { it - key }
    }

    /** Commits every delete still waiting, now: their window is cut short, not their delete. */
    fun commitAll() {
        val now = waiting.values.toList()
        waiting.clear()
        now.forEach { it.job.cancel() }
        _pending.value = emptySet()
        now.forEach { commit(it.item) }
    }

    companion object {
        /** Matches the snackbar's Short duration (4 s), so Undo is offered for the whole window. */
        const val UNDO_WINDOW_MS = 4_000L
    }
}
