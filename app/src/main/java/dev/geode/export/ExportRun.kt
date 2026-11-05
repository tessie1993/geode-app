package dev.geode.export

import dev.geode.engine.bridge.GeodeNative
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

object ExportRun {
    val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    data class State(
        val running: Boolean = false,
        val progress: Float? = null,
        val label: String = "",
        val secondsRemaining: Long? = null,
    )

    /**
     * The render-ETA state `core/output/RenderEta` steps on each [publish]. Kotlin only holds it; all
     * zeros is a fresh estimate, so [begin] and [finish] clear it without a native call.
     */
    private val eta = DoubleArray(GeodeNative.ETA_STATE_DOUBLES)

    private val _state = MutableStateFlow(State())
    val state: StateFlow<State> = _state

    val running: Boolean get() = _state.value.running

    @Volatile
    var cancelRequested: Boolean = false
        private set

    /**
     * The coroutine running the render now, kept here rather than in the controller that started
     * it. The controller belongs to the UI's session and is rebuilt when the Activity is; the render
     * is not. A Cancel pressed on the rebuilt UI used to set a flag on the new controller that the
     * old render's `isCancelled` lambda never read, so the button did nothing. Held process-wide,
     * any caller can stop the one render there is.
     */
    @Volatile
    private var job: Job? = null

    /**
     * Starts [block] as the render on [scope] and remembers it for [requestCancel]. Call after
     * [begin]; the block's own `finally` should call [finish].
     */
    fun launch(block: suspend CoroutineScope.() -> Unit): Job = scope.launch(block = block).also { job = it }

    /**
     * Stops the render, whoever asks: the Cancel button (whichever controller drew it), or
     * [ExportService] when the platform times the foreground service out.
     *
     * Two levers, because neither reaches everything alone. The flag is what the encoders' frame
     * loops poll: they run blocking GL and MediaCodec work with no suspension point, so a
     * cancelled job would carry on to the last frame, and the flag is also what sends them down
     * their own cleanup (the pending MediaStore row or SAF document deleted, scratch loops
     * discarded). Cancelling the job then ends whatever is suspended (the loudness read, the step
     * from loop render to extend, the gap between batch tracks) and makes the run end as
     * cancelled: the callers' `catch` blocks see a [kotlinx.coroutines.CancellationException],
     * reset their UI state and rethrow, and their `finally` still calls [finish], which is what
     * stops [ExportService].
     */
    fun requestCancel() {
        cancelRequested = true
        job?.cancel()
    }

    fun begin(label: String) {
        eta.fill(0.0)
        cancelRequested = false
        _state.value = State(running = true, progress = null, label = label)
    }

    /**
     * [steady] is false for the quick lead-in phase (the audio transcode): it moves the
     * bar but says nothing about how fast the render will go, so the native estimate skips it.
     */
    fun publish(
        progress: Float,
        steady: Boolean = true,
        atMs: Long = android.os.SystemClock.elapsedRealtime(),
    ) {
        val current = _state.value
        if (!current.running) return
        val clamped = progress.coerceIn(0f, 1f)
        val left = GeodeNative.renderEtaSample(eta, clamped, atMs, steady)
        _state.value = current.copy(progress = clamped, secondsRemaining = left.takeIf { it >= 0 })
    }

    fun finish() {
        job = null
        eta.fill(0.0)
        _state.value = State()
    }
}
