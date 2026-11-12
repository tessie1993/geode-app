package dev.geode.ui

import android.app.Application
import dev.geode.R
import dev.geode.data.RecordFiles
import dev.geode.data.RecordPrefs
import dev.geode.data.SaveTarget
import dev.geode.editor.ProjectRender
import dev.geode.editor.TrackRef
import dev.geode.engine.bridge.GeodeNative
import dev.geode.export.WatermarkOverlay
import dev.geode.model.WatermarkChoice
import dev.geode.pro.RenderLimits
import dev.geode.record.RecordEncoder
import dev.geode.render.VisualizerView
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
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

data class RecordUiState(
    val recording: Boolean = false,
    val recordedMs: Long = 0L,
    val note: String? = null,
    /** A finished recording with nowhere to go yet (the first one): its title, until `RecordSaveSheet` is answered. */
    val pendingSave: String? = null,
)

/**
 * Record (Pro): a video of what is on the stage and what it hears, captured live until the user
 * stops it. The stage's GL thread copies each finished frame into an H.264 encoder's surface and
 * hands every block its sound engine took (the song, the mic layer, another app's audio) to the
 * native clock (`core/output/Recorder`), which times the whole file; [RecordEncoder] moves the bytes
 * into the MP4. This class only starts and stops it, says why it ended, and saves the result:
 * straight into the place chosen in `RecordSaveSheet`, asked after the first recording, and onto
 * My videos as a project ([Host.keep]).
 *
 * Main thread, apart from the encoder thread's two callbacks, which hop back.
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
        val encoder: RecordEncoder,
        val output: RecordFiles.Output,
        val stage: VisualizerView,
        val watermark: WatermarkOverlay?,
        val track: TrackRef?,
    ) {
        /** Set once [finished] ran, which can beat [start]'s own bookkeeping when the encoder fails at once. */
        var done = false
        val teeClosing = AtomicBoolean(false)

        // Two owners of the native handle: the GL thread's queued tee events and [finished]; the last one frees it.
        private val owners = AtomicInteger(2)

        fun release() {
            if (owners.decrementAndGet() == 0) GeodeNative.recorderDestroy(recorder)
        }
    }

    private class Waiting(
        val file: File,
        val title: String,
        val durationMs: Long,
        val track: TrackRef?,
    )

    private val _state = MutableStateFlow(RecordUiState())
    val state: StateFlow<RecordUiState> = _state

    private var stage: VisualizerView? = null
    private var active: Active? = null
    private var waiting: Waiting? = null
    private var ticker: Job? = null

    /** The stage on screen, or null once it goes; a recording without its stage stops. */
    fun attach(view: VisualizerView?) {
        if (view == null && active?.stage === stage) stop()
        stage = view
    }

    fun start() {
        if (_state.value.recording) return
        if (!host.canRecord()) {
            host.onRecordLocked()
            return
        }
        val view = stage ?: return
        val width = view.visualizerRenderer.surfaceWidth
        val height = view.visualizerRenderer.surfaceHeight
        if (width <= 0 || height <= 0) return
        _state.update { it.copy(recording = true, recordedMs = 0L, note = null) }
        scope.launch {
            val started = open(view, width, height)
            if (started == null) {
                _state.update { it.copy(recording = false) }
                showNote(application.getString(R.string.record_failed))
                return@launch
            }
            if (started.done) return@launch
            active = started
            tee(started)
            ticker =
                scope.launch {
                    while (isActive) {
                        _state.update { it.copy(recordedMs = GeodeNative.recorderRecordedMs(started.recorder, GeodeNative.recordNowNs())) }
                        delay(TICK_MS)
                    }
                }
        }
    }

    /** Ends the recording; the encoder thread finishes the file and [finished] saves it. */
    fun stop() {
        val a = active ?: return
        GeodeNative.recorderStop(a.recorder, GeodeNative.RECORD_STOP_USER, GeodeNative.recordNowNs())
    }

    private suspend fun open(
        view: VisualizerView,
        width: Int,
        height: Int,
    ): Active? {
        val limits = host.limits()
        val track = host.track()
        return withContext(Dispatchers.IO) {
            val prefs = host.prefs()
            val recorder = GeodeNative.recorderCreate(width, height, prefs.fps, prefs.maxLengthSec, limits.capMs ?: 0L)
            if (recorder == 0L) return@withContext null
            val output = files.open(System.currentTimeMillis())
            val encoder = output?.let { RecordEncoder.open(it.fd, recorder, width, height, prefs.quality.shortSide, prefs.fps) }
            if (output == null || encoder == null) {
                output?.finish(keep = false)
                GeodeNative.recorderDestroy(recorder)
                return@withContext null
            }
            val watermark = WatermarkOverlay(limits.watermark, application, WatermarkChoice.USE_LIVE)
            val a = Active(recorder, encoder, output, view, watermark, track)
            val running =
                encoder.start(
                    onStopping = { closeTee(a) },
                    onFinished = { saved -> scope.launch { finished(a, saved) } },
                )
            if (running) {
                a
            } else {
                output.finish(keep = false)
                GeodeNative.recorderDestroy(recorder)
                null
            }
        }
    }

    /** GL thread: the stage starts copying into the encoder; a surface it cannot open ends the recording. */
    private fun tee(a: Active) {
        val renderer = a.stage.visualizerRenderer
        a.stage.queueEvent {
            val teed =
                runCatching {
                    a.watermark?.prepare(a.encoder.width, a.encoder.height)
                    renderer.recordStart(a.recorder, a.encoder.surface, a.encoder.width, a.encoder.height, a.watermark?.handle ?: 0L)
                }.getOrDefault(false)
            if (!teed) GeodeNative.recorderStop(a.recorder, GeodeNative.RECORD_STOP_FAILED, GeodeNative.recordNowNs())
        }
    }

    /** Encoder thread, once stopped: the stage lets go of the surface, then the video stream may end. */
    private fun closeTee(a: Active) {
        if (a.teeClosing.getAndSet(true)) return
        val renderer = a.stage.visualizerRenderer
        a.stage.queueEvent {
            renderer.recordStop()
            a.watermark?.release()
            a.encoder.teeClosed()
            a.release()
        }
    }

    private suspend fun finished(
        a: Active,
        saved: Boolean,
    ) {
        a.done = true
        ticker?.cancel()
        ticker = null
        val now = GeodeNative.recordNowNs()
        val recordedMs = GeodeNative.recorderRecordedMs(a.recorder, now)
        val why = GeodeNative.recorderStopped(a.recorder)
        val limitMs = GeodeNative.recorderLimitMs(a.recorder)
        a.release()
        if (active === a) active = null
        _state.update { it.copy(recording = false, recordedMs = 0L) }
        val keep = saved && GeodeNative.recordWorthKeeping(recordedMs)
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
            waiting = Waiting(scratch, title, recordedMs, a.track)
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
            GeodeNative.RECORD_STOP_MAX_LENGTH ->
                application.getString(R.string.record_stopped_at_max, recordLengthLabel(application, (limitMs / MS_PER_SECOND).toInt()))
            GeodeNative.RECORD_STOP_RESIZED -> application.getString(R.string.record_stopped_resized)
            GeodeNative.RECORD_STOP_FILE_FULL -> application.getString(R.string.record_stopped_full)
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
        w: Waiting,
        target: SaveTarget,
    ) {
        waiting = null
        val uri = withContext(Dispatchers.IO) { files.place(w.file, target) }
        if (uri == null) {
            showNote(application.getString(R.string.record_failed))
        } else {
            host.keep(w.track, ProjectRender(uri.toString(), w.title, System.currentTimeMillis(), w.durationMs))
        }
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
        const val MS_PER_SECOND = 1_000L
    }
}
