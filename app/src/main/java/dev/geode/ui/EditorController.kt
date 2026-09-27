package dev.geode.ui

import dev.geode.data.EditorProjectStore
import dev.geode.data.PresetStore
import dev.geode.editor.ClipId
import dev.geode.editor.EditResult
import dev.geode.editor.EditorHistory
import dev.geode.editor.EditorProject
import dev.geode.editor.KeyframeId
import dev.geode.editor.MarkerId
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.UUID

data class EditorUiState(
    val name: String = EditorController.DEFAULT_PROJECT,
    val history: EditorHistory = EditorHistory(EditorProject()),
    val loaded: Boolean = false,
    val playheadMs: Long = 0L,
) {
    val project: EditorProject get() = history.present
}

/**
 * Owns the open project: every edit goes through [edit] so it lands on the undo stack and on disk.
 * Edits run on the main thread; the store is touched only on [storeScope], one write at a time.
 */
internal class EditorController(
    private val store: EditorProjectStore,
    private val scope: CoroutineScope,
    private val storeScope: CoroutineScope,
) {
    private val _state = MutableStateFlow(EditorUiState())
    val state: StateFlow<EditorUiState> = _state

    private val _projectNames = MutableStateFlow<List<String>>(emptyList())
    val projectNames: StateFlow<List<String>> = _projectNames
    private var openJob: Job? = null

    fun refreshProjectNames() {
        scope.launch {
            _projectNames.value = withContext(storeScope.coroutineContext) { store.names() }
        }
    }

    fun open(name: String = DEFAULT_PROJECT) {
        if (_state.value.loaded && _state.value.name == name) return
        openJob?.cancel()
        _state.update { it.copy(loaded = false) }
        openJob =
            scope.launch {
                val loaded = withContext(storeScope.coroutineContext) { store.load(name) } ?: EditorProject()
                _state.value = EditorUiState(name = name, history = EditorHistory(loaded), loaded = true)
                refreshProjectNames()
            }
    }

    fun create(
        name: String,
        onCreated: (Boolean) -> Unit,
    ) {
        val clean = name.trim()
        if (clean.isEmpty() || clean != PresetStore.safeFileName(clean)) {
            onCreated(false)
            return
        }
        scope.launch {
            val saved =
                withContext(storeScope.coroutineContext) {
                    if (store.fileOf(clean).exists()) false else store.save(clean, EditorProject())
                }
            if (saved) {
                open(clean)
                refreshProjectNames()
            }
            onCreated(saved)
        }
    }

    fun edit(transform: (EditorProject) -> EditorProject) {
        val current = _state.value
        if (!current.loaded) return
        val next = current.history.push(transform(current.project))
        if (next === current.history) return
        _state.value = current.copy(history = next)
        autosave(current.name, next.present)
    }

    fun apply(result: EditResult) = edit { it.apply(result) }

    fun undo() = step(EditorHistory::undo)

    fun redo() = step(EditorHistory::redo)

    fun setPlayhead(ms: Long) {
        _state.update { it.copy(playheadMs = ms.coerceAtLeast(0L)) }
    }

    fun newClipId(): ClipId = ClipId(UUID.randomUUID().toString())

    fun newMarkerId(): MarkerId = MarkerId(UUID.randomUUID().toString())

    fun newKeyframeId(): KeyframeId = KeyframeId(UUID.randomUUID().toString())

    private fun step(move: (EditorHistory) -> EditorHistory) {
        val current = _state.value
        if (!current.loaded) return
        val next = move(current.history)
        if (next === current.history) return
        _state.value = current.copy(history = next)
        autosave(current.name, next.present)
    }

    private fun autosave(
        name: String,
        project: EditorProject,
    ) {
        storeScope.launch {
            store.save(name, project)
            _projectNames.value = store.names()
        }
    }

    companion object {
        const val DEFAULT_PROJECT = "Untitled"
    }
}
