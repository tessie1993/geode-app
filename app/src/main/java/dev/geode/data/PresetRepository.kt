package dev.geode.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import java.io.File

data class PresetFolders(
    val folders: List<String> = emptyList(),
    val folderByPreset: Map<String, String> = emptyMap(),
) {
    fun folderOf(preset: String): String = folderByPreset[preset].orEmpty()
}

interface PresetRepository {
    val folders: StateFlow<PresetFolders>

    suspend fun list(): List<Preset>

    suspend fun save(
        preset: Preset,
        folder: String = "",
    )

    suspend fun delete(name: String)

    suspend fun addFolder(path: String)

    suspend fun renameFolder(
        from: String,
        to: String,
    )

    suspend fun moveToFolder(
        name: String,
        folder: String,
    )

    suspend fun refreshFolders()

    fun fileOf(name: String): File?

    fun materializeMilk(
        presetName: String,
        source: String?,
    ): String?
}

class FilePresetRepository(
    private val store: PresetStore,
) : PresetRepository {
    private val _folders = MutableStateFlow(PresetFolders())
    override val folders: StateFlow<PresetFolders> = _folders.asStateFlow()

    override suspend fun list(): List<Preset> = withContext(Dispatchers.IO) { store.list() }

    override suspend fun save(
        preset: Preset,
        folder: String,
    ) {
        withContext(Dispatchers.IO) { store.save(preset, folder) }
        refreshFolders()
    }

    override suspend fun delete(name: String) {
        withContext(Dispatchers.IO) { store.delete(name) }
        refreshFolders()
    }

    override suspend fun addFolder(path: String) {
        withContext(Dispatchers.IO) { store.addFolder(path) }
        refreshFolders()
    }

    override suspend fun renameFolder(
        from: String,
        to: String,
    ) {
        withContext(Dispatchers.IO) { store.renameFolder(from, to) }
        refreshFolders()
    }

    override suspend fun moveToFolder(
        name: String,
        folder: String,
    ) {
        withContext(Dispatchers.IO) { store.moveToFolder(name, folder) }
        refreshFolders()
    }

    override suspend fun refreshFolders() {
        _folders.value =
            withContext(Dispatchers.IO) {
                PresetFolders(
                    folders = store.folders(),
                    folderByPreset = store.list().associate { it.name to store.folderOf(it.name) },
                )
            }
    }

    override fun fileOf(name: String): File? = store.fileOf(name)

    override fun materializeMilk(
        presetName: String,
        source: String?,
    ): String? = store.materializeMilk(presetName, source)
}
