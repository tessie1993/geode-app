package dev.geode.export

import dev.geode.engine.bridge.GeodeNative
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * The glue around the native render job (`core/output/RenderJob`, #433), which decides the one render at
 * a time, its progress, cancel and outcome. Kotlin keeps the coroutine and [state], the native snapshot
 * mirrored after every transition for the UI and [ExportService]. Enum ordinals are the native codes.
 */
object ExportRun {
    val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    /** Which render holds the job, so a rebuilt screen mirrors the right state (#372). */
    enum class Kind { RENDER, BATCH, PROJECT, LOOP }

    /** How a render ended as its caller saw it; [finish] resolves it. */
    enum class Outcome { SAVED, FAILED, CANCELLED, LOCKED, NOTHING_TO_RENDER }

    data class State(
        val running: Boolean = false,
        val progress: Float? = null,
        val label: String = "",
        val secondsRemaining: Long? = null,
        val kind: Kind? = null,
    )

    private val snapshot = DoubleArray(GeodeNative.RENDER_JOB_DOUBLES)
    private var label = ""

    private val _state = MutableStateFlow(State())
    val state: StateFlow<State> = _state

    val running: Boolean get() = _state.value.running

    /** What the encoders' frame loops poll: blocking GL and MediaCodec work has no suspension point. */
    val cancelRequested: Boolean get() = GeodeNative.renderJobCancelRequested()

    /** The render's coroutine, held here: the controller is rebuilt with the Activity, the render is not. */
    @Volatile
    private var job: Job? = null

    /** Starts [block] as the render on [scope]. Call after [begin]; the block's `finally` calls [finish]. */
    fun launch(block: suspend CoroutineScope.() -> Unit): Job = scope.launch(block = block).also { job = it }

    /**
     * Stops the render, whoever asks (any Cancel, or [ExportService]'s timeout): the native flag sends the frame
     * loops down their own cleanup, cancelling the job ends whatever is suspended; the `finally` still finishes.
     */
    fun requestCancel() {
        if (GeodeNative.renderJobRequestCancel()) mirror()
        job?.cancel()
    }

    /** False when another render holds the job. */
    fun begin(
        kind: Kind,
        label: String,
    ): Boolean {
        if (!GeodeNative.renderJobBegin(kind.ordinal)) return false
        this.label = label
        mirror()
        return true
    }

    /** [steady] is false for the quick lead-in (the audio transcode), which the native estimate skips. */
    fun publish(
        progress: Float,
        steady: Boolean = true,
    ) {
        if (GeodeNative.renderJobPublish(progress, android.os.SystemClock.elapsedRealtime(), steady)) mirror()
    }

    /** Ends the render with what its caller saw; answers the native resolution, null when none was live. */
    fun finish(outcome: Outcome): Outcome? {
        job = null
        return Outcome.entries.getOrNull(GeodeNative.renderJobFinish(outcome.ordinal)).also { mirror() }
    }

    /** Read and published under one lock, so a stale snapshot never overwrites a newer one. */
    @Synchronized
    private fun mirror() {
        GeodeNative.renderJobSnapshot(snapshot)
        val live = snapshot[0].toInt() in GeodeNative.RENDER_PHASE_PREPARING..GeodeNative.RENDER_PHASE_CANCELLING
        _state.value =
            if (!live) {
                State()
            } else {
                val progress = snapshot[4].toFloat().takeIf { snapshot[3] != 0.0 }
                State(true, progress, label, snapshot[5].toLong().takeIf { it >= 0 }, Kind.entries[snapshot[1].toInt()])
            }
    }
}
