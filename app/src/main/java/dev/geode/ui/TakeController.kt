package dev.geode.ui

import dev.geode.data.PerformanceTake
import dev.geode.data.SaveTarget
import dev.geode.data.TakeInfo
import dev.geode.data.TakeRepository
import dev.geode.data.TakeVideo
import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectRender
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
    /**
     * A take saved on the first stop that has nowhere to go yet: the ask-once save sheet
     * (`RecordSaveSheet`) shows while this is set, and [TakeController.chooseSaveTarget] answers it.
     */
    val pendingSave: String? = null,
)

/**
 * Records, replays and lists takes.
 *
 * A take is a screen capture of the live visual (ADR 0008): while recording, the stage's own frames
 * and the sound the performer hears are encoded natively into one MP4 ([Host.beginCapture],
 * [Host.endCapture]; `core/output/TakeSession`), so the video is exactly what was on screen,
 * director, reactions, camera and touch included, and never the sheet or the mini player.
 *
 * A stopped recording also becomes a Studio project (see `data/TakeStore`): the song on an audio lane
 * and the look changes as a visual clip, both on track time, timed by the song position
 * ([Host.trackPositionMs]) whenever a song is loaded and by the wall clock otherwise. The captured
 * MP4 is the project's render ([Host.recordRender]), which is how Studio › My videos lists it.
 *
 * Record is Pro and the trial ([Host.canRecord]); a trial take is limited like a trial render, a
 * minute and the wordmark, both applied natively ([Host.captureReachedCap] says when to stop). Where a take's video goes is asked once: the first stop leaves
 * [TakeUiState.pendingSave] set until [chooseSaveTarget]; later takes go straight to the stored
 * answer. Nothing renders after the stop: the video already exists and is only copied there.
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

        /** Starts capturing the stage; false when it cannot. Blocking: called on the store dispatcher. */
        fun beginCapture(): Boolean

        /** The running capture reached the entitlement's cap (a trial take's minute); native decides. */
        fun captureReachedCap(): Boolean

        /** Ends the capture: its finished MP4, or null when it holds no picture. Blocking, store dispatcher. */
        fun endCapture(): TakeVideo?

        /** Copies [video] to [target]: the saved video's URI, or null when the copy failed. Blocking. */
        fun publishCapture(
            video: TakeVideo,
            target: SaveTarget,
        ): String?

        /** Deletes a capture nobody keeps (a mis-tap). Blocking. */
        fun discardCapture(video: TakeVideo)

        /** Keeps [render] on project [id]'s list of renders. Main thread. */
        fun recordRender(
            id: ProjectId,
            render: ProjectRender,
        )
    }

    private val _state = MutableStateFlow(TakeUiState())

    val state: StateFlow<TakeUiState> = _state

    private var recorder: PerformanceTake.Recorder? = null

    /** The capture waiting for the save sheet's answer, beside [TakeUiState.pendingSave]. */
    @Volatile
    private var pendingVideo: TakeVideo? = null

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
        // The capture starts on the store dispatcher, in order before any stop that follows it; a stage
        // that cannot be captured ends the recording again with a note.
        storeScope.launch {
            if (!host.beginCapture()) scope.launch { abandonRecording() }
        }
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
                    // A trial take stops at its cap like a trial render; the file already ends there.
                    if (host.captureReachedCap()) stopRecording()
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

    /** The capture would not start: the recording ends at once, keeping nothing. */
    private fun abandonRecording() {
        if (recorder == null) return
        stopSampling()
        recorder = null
        _state.update { it.copy(recording = false, recordedEvents = 0, recordedMs = 0L) }
        showNote(CAPTURE_FAILED_NOTE)
    }

    private fun stopSampling() {
        recordJob?.cancel()
        recordJob = null
        recordTickJob?.cancel()
        recordTickJob = null
    }

    fun stopRecording(name: String? = null) {
        val rec = recorder ?: return
        stopSampling()
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
        val tooShort = maxOf(rec.durationMs, stopMs - rec.startTrackMs) < MIN_TAKE_MS
        storeScope.launch {
            val video = host.endCapture()
            when {
                tooShort -> {
                    video?.let(host::discardCapture)
                    showNote(TAKE_DISCARDED_NOTE)
                }
                video == null -> showNote(CAPTURE_EMPTY_NOTE)
                else -> {
                    val label = requested ?: defaultTakeName()
                    val saved = takes.save(label, pending(label))
                    onTakeSaved(saved, video)
                }
            }
            refresh()
        }
    }

    /**
     * After take [saved] is on disk with its capture [video]: copy the video where takes go, or, when
     * that has not been answered yet, raise the ask-once save sheet ([TakeUiState.pendingSave]) and
     * wait for [chooseSaveTarget]. Split from [stopRecording] so the rule is testable without a
     * recorder.
     */
    @androidx.annotation.VisibleForTesting
    internal suspend fun onTakeSaved(
        saved: String,
        video: TakeVideo,
    ) {
        val target = takes.saveTarget()
        if (target == null) {
            pendingVideo = video
            _state.update { it.copy(pendingSave = saved) }
        } else {
            place(saved, video, target)
        }
    }

    /**
     * The answer to [TakeUiState.pendingSave]: the waiting take goes to [target], which is kept as
     * the default for every later take only when [remember] is set (the sheet's "use for every
     * take", on by default). A target that cannot be used (a folder whose grant would not hold)
     * leaves the sheet up with a note, so the take is never sent to a folder it cannot write.
     */
    fun chooseSaveTarget(
        target: SaveTarget,
        remember: Boolean = true,
    ) {
        val name = _state.value.pendingSave ?: return
        val video = pendingVideo ?: return
        pendingVideo = null
        _state.update { it.copy(pendingSave = null) }
        storeScope.launch {
            if (takes.chooseSaveTarget(target, remember)) {
                place(name, video, target)
            } else {
                pendingVideo = video
                _state.update { it.copy(pendingSave = name) }
                showNote(TAKE_FOLDER_REFUSED_NOTE)
            }
        }
    }

    /**
     * The save sheet was dismissed. The take is already a video and must not be lost, so it goes to
     * the Videos library (no permission needed) this once; nothing is kept, and the next stop asks
     * again.
     */
    fun skipPendingSave() {
        val name = _state.value.pendingSave ?: return
        val video = pendingVideo
        pendingVideo = null
        _state.update { it.copy(pendingSave = null) }
        if (video != null) storeScope.launch { place(name, video, SaveTarget.VideosLibrary) }
    }

    /**
     * Copies take [name]'s capture to [saveTo] and lists it on the take's project. The target is
     * also written into the project's export settings, so anything rendered from the project later
     * goes to the same place.
     */
    private suspend fun place(
        name: String,
        video: TakeVideo,
        saveTo: SaveTarget,
    ) {
        val uri = host.publishCapture(video, saveTo)
        if (uri == null) {
            showNote(TAKE_NOT_SAVED_NOTE)
            return
        }
        val project = takes.projectToRender(name, saveTo) ?: return
        val render = ProjectRender(uri, name, System.currentTimeMillis(), video.durationMs)
        scope.launch { host.recordRender(project.id, render) }
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

        const val TAKE_NOT_SAVED_NOTE = "Couldn't save the take's video — check the folder and try again"

        const val CAPTURE_FAILED_NOTE = "Couldn't start recording on this screen"

        const val CAPTURE_EMPTY_NOTE = "Nothing was on screen — take not saved"

        const val TAKE_FOLDER_REFUSED_NOTE = "Can't save to that folder — pick another"

        /** A take shorter than this is a mis-tap, not a performance. */
        const val MIN_TAKE_MS = 1_000L

        const val TAKE_NOTE_MS = 4_000L
    }
}
