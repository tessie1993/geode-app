package dev.geode.export

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

object ExportRun {
    val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    data class State(
        val running: Boolean = false,
        val progress: Float? = null,
        val label: String = "",
        val secondsRemaining: Long? = null,
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

    fun begin(label: String) {
        eta.reset()
        cancelRequested = false
        _state.value = State(running = true, progress = null, label = label)
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

    fun finish() {
        eta.reset()
        _state.value = State()
    }
}
