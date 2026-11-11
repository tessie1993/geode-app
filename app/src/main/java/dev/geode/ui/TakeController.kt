package dev.geode.ui

import dev.geode.data.PerformanceTake
import dev.geode.data.SaveTarget
import dev.geode.data.TakeInfo
import dev.geode.data.TakeRepository
import dev.geode.data.VizUiState
import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectRender
import dev.geode.editor.StudioProject
import dev.geode.export.ExportRun
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

private const val TAKE_REPLAY_HZ = 30L

data class TakeUiState(
    val takes: List<TakeInfo> = emptyList(),
    val recording: Boolean = false,
    val recordedEvents: Int = 0,
    val recordedMs: Long = 0L,
    val replaying: String? = null,
    val replayMs: Long = 0L,
    val replayEndMs: Long = 0L,
    val exportTake: String? = null,
    val note: String? = null,
    /** A take saved on the first stop with nowhere to render yet: `RecordSaveSheet` asks until [TakeController.chooseSaveTarget]. */
    val pendingSave: String? = null,
)

/**
 * Records, replays and lists takes. A stopped recording becomes a Studio project (see
 * `data/TakeStore`): the song on an audio lane and the take as a visual clip, both on track time.
 *
 * The recorder is timed by the song position ([Host.trackPositionMs]) whenever a song is loaded, so
 * pausing, seeking forward and seeking back (a punch-in) all keep the take on the beat; with no song
 * (mic or another app's audio) it falls back to the wall clock.
 *
 * Record is Pro ([Host.canRecord]). Every saved take renders to an MP4 in the background through the
 * project render ([Host.renderProject], under `ExportRun` and `ExportService`), and the finished
 * video is recorded on the take's project ([Host.recordRender]), which is how Studio › My videos
 * lists it. Where it renders to is asked once: the first stop leaves [TakeUiState.pendingSave] set
 * until [chooseSaveTarget]; later takes go straight to the stored answer.
 */
internal class TakeController(
    private val takes: TakeRepository,
    private val scope: CoroutineScope,
    private val storeScope: CoroutineScope,
    private val host: Host,
    /** The default name of the [n]th take, e.g. "Take 2". */
    private val takeName: (n: Int) -> String,
) {
    interface Host {
        val vizState: StateFlow<VizUiState>
        val activeMilkPath: String?

        val trackUri: String?

        val trackPositionMs: Long

        fun selectScene(sceneId: String)

        fun setSceneParams(params: SceneParams)

        fun applyMilk(
            path: String,
            sceneId: String,
        )

        /** Whether this user may record (Pro and the trial). */
        fun canRecord(): Boolean

        /** A refused recording: opens the paywall. */
        fun onRecordLocked()

        /** Renders [project] in the background; [onRendered] gets the finished video. */
        fun renderProject(
            project: StudioProject,
            onRendered: (ProjectRender) -> Unit,
        )

        /** Keeps [render] on project [id]'s list of renders. */
        fun recordRender(
            id: ProjectId,
            render: ProjectRender,
        )
    }

    private val _state = MutableStateFlow(TakeUiState())

    val state: StateFlow<TakeUiState> = _state

    private var recorder: PerformanceTake.Recorder? = null

    /** Wall-clock start, for takes recorded with no song. */
    private var wallStartMs = 0L
    private var recordTrackUri: String? = null
    private var recordJob: Job? = null
    private var recordTickJob: Job? = null
    private var replayJob: Job? = null

    fun startRecording() {
        if (_state.value.recording) return
        if (!host.canRecord()) {
            host.onRecordLocked()
            return
        }
        stopReplay()
        val s = host.vizState.value
        recordTrackUri = host.trackUri
        wallStartMs = android.os.SystemClock.elapsedRealtime()
        recorder = PerformanceTake.Recorder(s.sceneId, s.params, host.activeMilkPath, startTrackMs = takeClockMs())
        _state.update { it.copy(recording = true, recordedEvents = 1, recordedMs = 0L, note = null) }
        recordJob =
            scope.launch {
                host.vizState.collect { live -> sample(live) }
            }
        recordTickJob =
            scope.launch {
                while (true) {
                    delay(1_000L)
                    // Sampling with an unchanged look writes nothing, but it notices a seek back (a
                    // punch-in) or a new song even while nobody touches the visual.
                    sample(host.vizState.value)
                }
            }
    }

    /**
     * One look at the live state while recording. A different song (the queue moved on) ends the
     * take first: its track time would otherwise read as a seek back and punch out everything
     * recorded. Repeat-one looping the same song reads as a seek back, a punch-in, like any other.
     */
    private fun sample(live: VizUiState) {
        val rec = recorder ?: return
        if (host.trackUri != recordTrackUri) {
            stopRecording()
            return
        }
        rec.append(takeClockMs(), live.sceneId, live.params, host.activeMilkPath)
        _state.update { it.copy(recordedEvents = rec.size, recordedMs = rec.durationMs) }
        if (!rec.hasRoom) stopRecording()
    }

    fun stopRecording(name: String? = null) {
        val rec = recorder ?: return
        recordJob?.cancel()
        recordJob = null
        recordTickJob?.cancel()
        recordTickJob = null
        recorder = null
        val live = host.vizState.value
        val trackUri = recordTrackUri
        val stopMs = takeClockMs()
        val requested = name?.takeIf { it.isNotBlank() }
        val milk = host.activeMilkPath
        // The look and song position are read now, so the take ends on what was on screen at stop;
        // the JSON is built once the name is known. Nothing else touches the recorder after this.
        val pending = { label: String -> rec.finish(label, trackUri, stopMs, live.sceneId, live.params, milk) }
        _state.update { it.copy(recording = false, recordedEvents = 0, recordedMs = 0L) }
        if (maxOf(rec.durationMs, stopMs - rec.startTrackMs) < MIN_TAKE_MS) {
            showNote(TAKE_DISCARDED_NOTE)
            refresh()
            return
        }
        storeScope.launch {
            val label = requested ?: defaultTakeName()
            val saved = takes.save(label, pending(label))
            refresh()
            onTakeSaved(saved)
        }
    }

    /**
     * After take [saved] is on disk: render it where takes go, or, when that has not been answered
     * yet, raise the ask-once save sheet ([TakeUiState.pendingSave]) and wait for
     * [chooseSaveTarget]. Split from [stopRecording] so the rule is testable without a recorder.
     */
    @androidx.annotation.VisibleForTesting
    internal suspend fun onTakeSaved(saved: String) {
        val target = takes.saveTarget()
        if (target == null) {
            _state.update { it.copy(pendingSave = saved) }
        } else {
            render(saved, target)
        }
    }

    /**
     * The answer to [TakeUiState.pendingSave]: the waiting take renders to [target], which is kept
     * as the default for every later take only when [remember] is set (the sheet's "use for every
     * take", on by default). A target that cannot be used (a folder whose grant would not hold)
     * leaves the sheet up with a note, so the take is never sent to a folder it cannot write.
     */
    fun chooseSaveTarget(
        target: SaveTarget,
        remember: Boolean = true,
    ) {
        val name = _state.value.pendingSave ?: return
        _state.update { it.copy(pendingSave = null) }
        storeScope.launch {
            if (takes.chooseSaveTarget(target, remember)) {
                render(name, target)
            } else {
                _state.update { it.copy(pendingSave = name) }
                showNote(TAKE_FOLDER_REFUSED_NOTE)
            }
        }
    }

    /** The save sheet was dismissed: the take stays in My videos, unrendered, and the next stop asks again. */
    fun skipPendingSave() {
        _state.update { it.copy(pendingSave = null) }
    }

    /**
     * Renders take [name] to [saveTo]. A render already running (an export, another take) owns the
     * encoder, so this take waits in My videos instead, with a note saying so.
     */
    private suspend fun render(
        name: String,
        saveTo: SaveTarget,
    ) {
        val project = takes.projectToRender(name, saveTo) ?: return
        scope.launch {
            if (ExportRun.running) {
                showNote(TAKE_NOT_RENDERED_NOTE)
                return@launch
            }
            host.renderProject(project) { finished -> host.recordRender(project.id, finished) }
        }
    }

    private fun showNote(note: String) {
        _state.update { it.copy(note = note) }
        scope.launch {
            delay(TAKE_NOTE_MS)
            _state.update { if (it.note == note) it.copy(note = null) else it }
        }
    }

    /** Song position while a song is loaded, else wall-clock time since recording began. */
    private fun takeClockMs(): Long =
        if (recordTrackUri != null) host.trackPositionMs else android.os.SystemClock.elapsedRealtime() - wallStartMs

    /** The project the take called [name] lives in, to open it in Edit. */
    suspend fun projectOf(name: String): ProjectId? = takes.projectOf(name)

    private suspend fun defaultTakeName(): String {
        val taken = takes.list().map { it.name }.toSet()
        var n = 1
        while (takeName(n) in taken) n++
        return takeName(n)
    }

    fun playTake(name: String) {
        if (_state.value.recording) stopRecording()
        stopReplay()
        replayJob =
            scope.launch {
                val timeline = takes.load(name) ?: return@launch
                if (timeline.isEmpty) return@launch
                val endMs = maxOf(timeline.lastEventMs(), timeline.durationMs)
                _state.update { it.copy(replaying = name, replayMs = 0L, replayEndMs = endMs) }
                val startedAt = android.os.SystemClock.elapsedRealtime()
                while (true) {
                    val at = android.os.SystemClock.elapsedRealtime() - startedAt
                    timeline.stateAt(at)?.let { state ->
                        if (state.sceneId.isNotEmpty() && state.sceneId != host.vizState.value.sceneId) {
                            host.selectScene(state.sceneId)
                        }
                        if (state.params != host.vizState.value.params) host.setSceneParams(state.params)
                        state.milkPath?.takeIf { it != host.activeMilkPath }?.let { path ->
                            host.applyMilk(path, state.sceneId)
                        }
                    }
                    _state.update { it.copy(replayMs = at) }
                    if (at >= endMs) break
                    delay(1000L / TAKE_REPLAY_HZ)
                }
                _state.update { it.copy(replaying = null, replayMs = 0L, replayEndMs = 0L) }
            }
    }

    fun stopReplay() {
        replayJob?.cancel()
        replayJob = null
        _state.update { it.copy(replaying = null, replayMs = 0L, replayEndMs = 0L) }
    }

    fun deleteTake(name: String) {
        if (_state.value.replaying == name) stopReplay()
        storeScope.launch {
            takes.delete(name)
            val listed = takes.list()
            _state.update { it.copy(takes = listed) }
        }
    }

    fun renameTake(
        from: String,
        to: String,
    ) {
        storeScope.launch {
            if (!takes.rename(from, to)) return@launch
            if (_state.value.replaying == from) stopReplay()
            refresh()
        }
    }

    fun refresh() {
        scope.launch {
            val listed = takes.list()
            _state.update { it.copy(takes = listed) }
        }
    }

    fun setExportTake(name: String?) {
        _state.update { it.copy(exportTake = name) }
    }

    suspend fun loadExportTake(): PerformanceTake.Timeline? =
        _state.value.exportTake
            ?.let { takes.load(it) }
            ?.takeUnless { it.isEmpty }

    private companion object {
        const val TAKE_DISCARDED_NOTE = "Too short — take not saved"

        const val TAKE_NOT_RENDERED_NOTE = "Take saved — another render is running, render it from My videos"

        const val TAKE_FOLDER_REFUSED_NOTE = "Can't save to that folder — pick another"

        /** A take shorter than this is a mis-tap, not a performance. */
        const val MIN_TAKE_MS = 1_000L

        const val TAKE_NOTE_MS = 4_000L
    }
}
