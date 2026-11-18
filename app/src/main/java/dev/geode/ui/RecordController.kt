package dev.geode.ui

import android.app.Application
import dev.geode.R
import dev.geode.data.RecordFiles
import dev.geode.data.RecordPrefs
import dev.geode.data.SaveTarget
import dev.geode.editor.ProjectRender
import dev.geode.editor.TrackRef
import dev.geode.engine.bridge.RecorderNative
import dev.geode.pro.RenderLimits
import dev.geode.stage.RecordMark
import dev.geode.stage.Stage
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.atomic.AtomicBoolean

data class RecordUiState(
    val recording: Boolean = false,
    val recordedMs: Long = 0L,
    val note: String? = null,
    /** A finished recording with nowhere to go yet (the first one): its title, until `RecordSaveSheet` is answered. */
    val pendingSave: String? = null,
)

/**
 * Record (Pro): a video of what is on the stage and what it hears, captured live until the user
 * stops it. The stage's thread copies each finished frame into an H.264 encoder's surface and
 * hands every block its sound engine took (the song, the mic layer, another app's audio) to the
 * native clock (`core/output/Recorder`), which times the whole file; the native session
 * (`core/output/RecordSession`) encodes and muxes on its own thread. This class only starts and
 * stops it, polls it, says why it ended, and saves the result: straight into the place chosen in
 * `RecordSaveSheet`, asked after the first recording, and onto My videos as a project ([Host.keep]).
 *
 * Main thread; the tee is a command to the stage.
 */
internal class RecordController(
    private val application: Application,
    private val scope: CoroutineScope,
    private val files: RecordFiles,
    private val host: Host,
) {
    interface Host {
        /** Pro and the trial. */
        fun canRecord(): Boolean

        fun onRecordLocked()

        /** Settings › Export › Record. Read off the main thread. */
        fun prefs(): RecordPrefs

        /** The entitlement's cap and watermark. */
        fun limits(): RenderLimits

        /** The song playing, or null for the mic or another app's audio. */
        fun track(): TrackRef?

        /** Lists a saved recording in My videos. */
        fun keep(
            track: TrackRef?,
            render: ProjectRender,
        )
    }

    private class Active(
        val recorder: Long,
        val session: Long,
        val size: IntArray,
        val output: RecordFiles.Output,
        val stage: Stage,
        val mark: RecordMark,
        val track: TrackRef?,
        val teeClosing: AtomicBoolean = AtomicBoolean(false),
    )

    private val _state = MutableStateFlow(RecordUiState())
    val state: StateFlow<RecordUiState> = _state

    private var stage: Stage? = null
    private var active: Active? = null
    private var waiting: RecordFiles.Waiting? = null
    private var ticker: Job? = null
    private var stopRequested = false

    // A recording an earlier process parked for the sheet and never placed is asked about again.
    init {
        scope.launch {
            val w = withContext(Dispatchers.IO) { files.waiting() } ?: return@launch
            if (waiting == null) waiting = w.also { _state.update { it.copy(pendingSave = w.title) } }
        }
    }

    /** The stage on screen, or null once it goes; a recording without its stage stops (one still opening, once open). */
    fun attach(next: Stage?) {
        if (next == null && (active?.stage ?: stage) === stage) stop()
        stage = next
    }

    fun start() {
        if (_state.value.recording) return
        if (!host.canRecord()) {
            host.onRecordLocked()
            return
        }
        val view = stage ?: return
        val width = view.surfaceWidth
        val height = view.surfaceHeight
        if (width <= 0 || height <= 0) return
        stopRequested = false
        _state.update { it.copy(recording = true, recordedMs = 0L, note = null) }
        scope.launch {
            val started = open(view, width, height)
            if (started == null) {
                _state.update { it.copy(recording = false) }
                showNote(application.getString(R.string.record_failed))
                return@launch
            }
            active = started
            tee(started)
            if (stopRequested) stop()
            ticker =
                scope.launch {
                    while (isActive) {
                        val phase = RecorderNative.recordSessionState(started.session)
                        if (phase >= RecorderNative.RECORD_SESSION_CLOSING) closeTee(started)
                        if (phase >= RecorderNative.RECORD_SESSION_SAVED) {
                            scope.launch { finished(started, phase == RecorderNative.RECORD_SESSION_SAVED) }
                            break
                        }
                        _state.update {
                            it.copy(recordedMs = RecorderNative.recorderRecordedMs(started.recorder, RecorderNative.recordNowNs()))
                        }
                        delay(TICK_MS)
                    }
                }
        }
    }

    /** Ends the recording; the session finishes the file and [finished] saves it. */
    fun stop() {
        val a = active ?: return run { stopRequested = _state.value.recording }
        RecorderNative.recorderStop(a.recorder, RecorderNative.RECORD_STOP_USER, RecorderNative.recordNowNs())
        closeTee(a)
    }

    private suspend fun open(
        view: Stage,
        width: Int,
        height: Int,
    ): Active? {
        val limits = host.limits()
        val track = host.track()
        return withContext(Dispatchers.IO) {
            val prefs = host.prefs()
            val recorder = RecorderNative.recorderCreate(width, height, prefs.fps, prefs.maxLengthSec, limits.capMs ?: 0L)
            if (recorder == 0L) return@withContext null
            val output = files.open(System.currentTimeMillis())
            val shortSide = prefs.quality.shortSide
            val session = output?.let { RecorderNative.recordSessionOpen(recorder, it.rawFd, width, height, shortSide, prefs.fps) } ?: 0L
            if (output == null || session == 0L) {
                output?.finish(keep = false)
                RecorderNative.recorderDestroy(recorder)
                return@withContext null
            }
            val size = IntArray(2).also { RecorderNative.recordSessionSize(session, it) }
            Active(recorder, session, size, output, view, RecordMark(application, limits.watermark, size[0], size[1]), track)
        }
    }

    /** The stage starts copying into the encoder; native ends the recording as failed when it cannot. */
    private fun tee(a: Active) {
        a.stage.recordStart(a.recorder, a.session, a.mark)
    }

    /** Once stopped: the stage lets go of the encoder's surface and tells the session, so the video stream may end. */
    private fun closeTee(a: Active) {
        if (!a.teeClosing.getAndSet(true)) a.stage.recordStop()
    }

    private suspend fun finished(
        a: Active,
        saved: Boolean,
    ) {
        ticker?.cancel()
        ticker = null
        val recordedMs = RecorderNative.recorderRecordedMs(a.recorder, RecorderNative.recordNowNs())
        val why = RecorderNative.recorderStopped(a.recorder)
        val limitMs = RecorderNative.recorderLimitMs(a.recorder)
        // The handles go once the stage holds neither (a stage that is gone holds none); one that never does keeps them.
        val closed = withTimeoutOrNull(CLOSE_WAIT_MS) { while (!a.stage.recordClosed) delay(TICK_MS) } != null
        if (closed) {
            RecorderNative.recordSessionDestroy(a.session)
            RecorderNative.recorderDestroy(a.recorder)
        }
        if (active === a) active = null
        _state.update { it.copy(recording = false, recordedMs = 0L) }
        val keep = saved && RecorderNative.recordWorthKeeping(recordedMs)
        val uri = withContext(Dispatchers.IO) { a.output.finish(keep) }
        val title = a.track?.title?.takeIf { it.isNotBlank() } ?: application.getString(R.string.record_default_name)
        when {
            !saved -> showNote(application.getString(R.string.record_failed))
            !keep -> showNote(application.getString(R.string.record_too_short))
            else -> endNote(why, limitMs)?.let(::showNote)
        }
        if (uri == null) return
        val scratch = a.output.scratch
        if (scratch != null) {
            waiting = withContext(Dispatchers.IO) { files.park(scratch, title, recordedMs, a.track) }
            _state.update { it.copy(pendingSave = title) }
        } else {
            host.keep(a.track, ProjectRender(uri.toString(), title, System.currentTimeMillis(), recordedMs))
        }
    }

    private fun endNote(
        why: Int,
        limitMs: Long,
    ): String? =
        when (why) {
            RecorderNative.RECORD_STOP_MAX_LENGTH ->
                application.getString(R.string.record_stopped_at_max, recordLengthLabel(application, (limitMs / MS_PER_SECOND).toInt()))
            RecorderNative.RECORD_STOP_RESIZED -> application.getString(R.string.record_stopped_resized)
            RecorderNative.RECORD_STOP_FILE_FULL -> application.getString(R.string.record_stopped_full)
            else -> null
        }

    /**
     * The answer to [RecordUiState.pendingSave]: the waiting recording goes to [target], kept as the
     * place for every later recording when [remember] is set. A folder whose grant will not hold
     * leaves the sheet up with a note.
     */
    fun chooseSaveTarget(
        target: SaveTarget,
        remember: Boolean = true,
    ) {
        val w = waiting ?: return
        _state.update { it.copy(pendingSave = null) }
        scope.launch {
            val chosen = withContext(Dispatchers.IO) { files.saveTargets.choose(target, remember) }
            if (!chosen) {
                _state.update { it.copy(pendingSave = w.title) }
                showNote(application.getString(R.string.record_folder_refused))
                return@launch
            }
            place(w, target)
        }
    }

    /** The sheet was dismissed: this recording goes to the Videos library, and the next one asks again. */
    fun skipPendingSave() {
        val w = waiting ?: return
        _state.update { it.copy(pendingSave = null) }
        scope.launch { place(w, SaveTarget.VideosLibrary) }
    }

    private suspend fun place(
        w: RecordFiles.Waiting,
        target: SaveTarget,
    ) {
        waiting = null
        val uri = withContext(Dispatchers.IO) { files.place(w.file, target) }
        uri?.let { host.keep(w.track, ProjectRender(it.toString(), w.title, System.currentTimeMillis(), w.durationMs)) }
            ?: showNote(application.getString(R.string.record_failed))
    }

    private fun showNote(note: String) {
        _state.update { it.copy(note = note) }
        scope.launch {
            delay(NOTE_MS)
            _state.update { if (it.note == note) it.copy(note = null) else it }
        }
    }

    private companion object {
        const val TICK_MS = 250L
        const val NOTE_MS = 4_000L
        const val CLOSE_WAIT_MS = 5_000L
        const val MS_PER_SECOND = 1_000L
    }
}
