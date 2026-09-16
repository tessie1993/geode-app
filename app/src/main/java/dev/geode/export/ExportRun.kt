package dev.geode.export

import android.net.Uri
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * The single render in flight, shared by every export/render path (the visualizer export, a
 * studio clip export, a project cut export and a loop render) so that all of them get the same
 * foreground-service notification, ETA and cancel/resume behaviour, and so that at most one runs
 * at a time. [scope] outlives the Activity/ViewModel that started the render, which is the whole
 * point: leaving the app or recreating a controller must not silently kill a render in progress.
 */
object ExportRun {
    val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    /** Which render path owns the current run, so a resumed UI can route progress and results. */
    enum class Kind { Visualizer, Studio, Project, Loop }

    /** The outcome of a finished run, published through [finish] and read back on resume. */
    sealed interface Result {
        data class Saved(
            val uri: Uri,
        ) : Result

        data class Failed(
            val message: String,
        ) : Result

        data object Cancelled : Result
    }

    data class State(
        val running: Boolean = false,
        val kind: Kind? = null,
        val progress: Float? = null,
        val label: String = "",
        val secondsRemaining: Long? = null,
        /** The last run's outcome, kept until the owning controller reads it and calls [clear]. */
        val result: Result? = null,
    )

    private val eta = RenderEta()

    private val _state = MutableStateFlow(State())
    val state: StateFlow<State> = _state

    val running: Boolean get() = _state.value.running

    @Volatile
    var cancelRequested: Boolean = false
        private set

    fun requestCancel() {
        cancelRequested = true
    }

    fun begin(
        kind: Kind,
        label: String,
    ) {
        eta.reset()
        cancelRequested = false
        _state.value = State(running = true, kind = kind, progress = null, label = label)
    }

    fun publish(
        progress: Float,
        atMs: Long = android.os.SystemClock.elapsedRealtime(),
    ) {
        val current = _state.value
        if (!current.running) return
        val clamped = progress.coerceIn(0f, 1f)
        _state.value = current.copy(progress = clamped, secondsRemaining = eta.sample(clamped, atMs))
    }

    /** Ends the run, publishing [result] for the owning controller to read back on resume. */
    fun finish(result: Result) {
        eta.reset()
        cancelRequested = false
        _state.value = State(running = false, kind = _state.value.kind, result = result)
    }

    /**
     * Drops a finished run's stored result once the owning controller has shown it. Scoped to
     * [kind] so dismissing one render path's dialog cannot discard another path's still-unseen
     * result — e.g. a loop render that finished while the visualizer export dialog was open.
     */
    fun clear(kind: Kind) {
        if (!_state.value.running && _state.value.kind == kind) _state.value = State()
    }
}
