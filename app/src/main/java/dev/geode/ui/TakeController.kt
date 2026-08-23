package dev.geode.ui

import dev.geode.data.PerformanceTake
import dev.geode.data.TakeInfo
import dev.geode.data.TakeRepository
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
)

internal class TakeController(
    private val takes: TakeRepository,
    private val scope: CoroutineScope,
    private val storeScope: CoroutineScope,
    private val host: Host,
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
    }

    private val _state = MutableStateFlow(TakeUiState())

    val state: StateFlow<TakeUiState> = _state

    private var recorder: PerformanceTake.Recorder? = null
    private var recordStartMs = 0L

    private var recordTrackOffsetMs = 0L
    private var recordJob: Job? = null
    private var recordTickJob: Job? = null
    private var replayJob: Job? = null

    fun startRecording() {
        if (_state.value.recording) return
        stopReplay()
        val s = host.vizState.value
        recorder = PerformanceTake.Recorder(s.sceneId, s.params, host.activeMilkPath)
        recordStartMs = android.os.SystemClock.elapsedRealtime()
        recordTrackOffsetMs = host.trackPositionMs
        _state.update { it.copy(recording = true, recordedEvents = 1, recordedMs = 0L, note = null) }
        recordJob =
            scope.launch {
                host.vizState.collect { live ->
                    val rec = recorder ?: return@collect
                    val at = android.os.SystemClock.elapsedRealtime() - recordStartMs
                    rec.append(at, live.sceneId, live.params, host.activeMilkPath)
                    _state.update { it.copy(recordedEvents = rec.size, recordedMs = at) }
                    if (!rec.hasRoom) stopRecording()
                }
            }
        recordTickJob =
            scope.launch {
                while (true) {
                    delay(1_000L)
                    val at = android.os.SystemClock.elapsedRealtime() - recordStartMs
                    _state.update { if (it.recording) it.copy(recordedMs = at) else it }
                }
            }
    }

    fun stopRecording(name: String? = null) {
        val rec = recorder ?: return
        recordJob?.cancel()
        recordJob = null
        recordTickJob?.cancel()
        recordTickJob = null
        recorder = null
        val durationMs = android.os.SystemClock.elapsedRealtime() - recordStartMs
        _state.update { it.copy(recording = false, recordedEvents = 0, recordedMs = 0L) }
        if (rec.size <= 1) {
            _state.update { it.copy(note = TAKE_DISCARDED_NOTE) }
            scope.launch {
                delay(TAKE_NOTE_MS)
                _state.update { if (it.note == TAKE_DISCARDED_NOTE) it.copy(note = null) else it }
            }
            refresh()
            return
        }
        val trackUri = host.trackUri
        val requested = name?.takeIf { it.isNotBlank() }
        storeScope.launch {
            val label = requested ?: defaultTakeName()
            takes.save(label, rec.finish(label, trackUri, durationMs, recordTrackOffsetMs))
            refresh()
        }
    }

    private suspend fun defaultTakeName(): String {
        val taken = takes.list().map { it.name }.toSet()
        var n = 1
        while ("Take $n" in taken) n++
        return "Take $n"
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
        const val TAKE_DISCARDED_NOTE = "Nothing changed — take not saved"

        const val TAKE_NOTE_MS = 4_000L
    }
}
