package dev.geode.ui

import android.app.Application
import android.net.Uri
import dev.geode.data.BuiltInPresets
import dev.geode.data.Preset
import dev.geode.data.PresetFolders
import dev.geode.data.PresetRepository
import dev.geode.data.PresetStore
import dev.geode.data.ReadCap
import dev.geode.data.VizUiState
import dev.geode.model.PresetFiltering
import dev.geode.model.PresetLink
import dev.geode.render.scene.SceneIds
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

internal class PresetLibraryController(
    private val application: Application,
    private val presets: PresetRepository,
    private val scope: CoroutineScope,
    private val storeScope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        val vizState: StateFlow<VizUiState>

        fun updatePresets(transform: (List<Preset>) -> List<Preset>)

        val presetMirrorUri: String?

        val activeMilkPath: String?

        /** A saved MilkDrop look writes its .milk beside the user's own; the session's .milk list reads again. */
        fun onMilkFilesChanged()
    }

    val folders: StateFlow<PresetFolders> = presets.folders

    fun addPresetFolder(path: String) {
        storeScope.launch { presets.addFolder(path) }
    }

    fun renamePresetFolder(
        from: String,
        to: String,
    ) {
        storeScope.launch { presets.renameFolder(from, to) }
    }

    fun movePresetToFolder(
        name: String,
        folder: String,
    ) {
        storeScope.launch {
            presets.moveToFolder(name, folder)
            mirrorPresetToChosenFolder(name)
            relistPresets()
        }
    }

    fun refreshInitial() {
        scope.launch {
            val listed = presets.list()
            host.updatePresets { current ->
                if (current !== BuiltInPresets.ALL) current else BuiltInPresets.ALL + listed
            }
            presets.refreshFolders()
        }
    }

    private suspend fun relistPresets() {
        val listed = BuiltInPresets.ALL + presets.list()
        host.updatePresets { listed }
        host.onMilkFilesChanged()
    }

    fun savePreset(
        name: String,
        customShader: String?,
        folder: String = "",
    ) {
        @Suppress("NAME_SHADOWING")
        val name = name.replace(" · ", " - ").trim().ifEmpty { application.getString(dev.geode.R.string.preset_untitled) }
        val s = host.vizState.value
        val milkPath = host.activeMilkPath
        storeScope.launch {
            val milkSource =
                if (s.sceneId == SceneIds.MILKDROP) {
                    milkPath?.let { src -> runCatching { java.io.File(src).readText() }.getOrNull() }
                } else {
                    null
                }
            milkSource?.let { source -> presets.materializeMilk(name, source) }
            presets.save(Preset(name, s.sceneId, s.attack, s.decay, customShader, s.params, milkSource), folder)
            mirrorPresetToChosenFolder(name)
            relistPresets()
        }
    }

    private fun mirrorPresetToChosenFolder(name: String) {
        val uriStr = host.presetMirrorUri ?: return
        scope.launch(Dispatchers.IO) {
            runCatching {
                val tree =
                    androidx.documentfile.provider.DocumentFile
                        .fromTreeUri(application, Uri.parse(uriStr))
                        ?: return@runCatching

                fun copyInto(
                    src: java.io.File,
                    mime: String,
                ) {
                    if (!src.exists()) return
                    tree.findFile(src.name)?.delete()
                    val dest = tree.createFile(mime, src.name) ?: return
                    application.contentResolver.openOutputStream(dest.uri)?.use { out ->
                        src.inputStream().use { it.copyTo(out) }
                    }
                }
                presets.fileOf(name)?.let { copyInto(it, "application/json") }
                milkFileFor(name).let { copyInto(it, "text/plain") }
            }
        }
    }

    private fun milkFileFor(presetName: String): java.io.File =
        java.io.File(
            java.io.File(application.filesDir, "milk").apply { mkdirs() },
            PresetStore.milkFileName(presetName),
        )

    suspend fun milkPresetPathFor(preset: Preset): String? {
        if (preset.sceneId != SceneIds.MILKDROP) return null
        return withContext(Dispatchers.IO) { presets.materializeMilk(preset.name, preset.milkPreset) }
    }

    fun presetShareLink(name: String): String? {
        val preset =
            host.vizState.value.presets
                .firstOrNull { it.name == name } ?: return null
        val link = PresetLink.encode(PresetStore.toJson(preset))
        return link.takeIf { it.length <= PresetLink.MAX_LINK_LENGTH }
    }

    fun importPresetLink(text: String): String? {
        val link = PresetLink.findIn(text) ?: return null
        return importPresetJson(PresetLink.decode(link) ?: return null)
    }

    fun importPresetFile(
        uri: Uri,
        onResult: (String?) -> Unit,
    ) {
        scope.launch {
            val json =
                withContext(Dispatchers.IO) {
                    runCatching {
                        application.contentResolver.openInputStream(uri)?.use(ReadCap.PRESET::text)
                    }.getOrNull()
                }
            onResult(json?.let { importPresetJson(it) })
        }
    }

    private fun importPresetJson(json: String): String? {
        val incoming = runCatching { PresetStore.fromJson(json) }.getOrNull() ?: return null
        val existing =
            host.vizState.value.presets
                .map { it.name }
                .toSet()
        val base =
            incoming.name
                .replace(" · ", " - ")
                .trim()
                .ifBlank { application.getString(dev.geode.R.string.preset_shared_untitled) }
        var name = base
        var n = 2
        while (name in existing) {
            name = "$base $n"
            n++
        }
        val preset = incoming.copy(name = name)
        host.updatePresets { it + preset }
        storeScope.launch {
            presets.save(preset)
            relistPresets()
        }
        return name
    }

    fun presetFile(name: String): java.io.File? = presets.fileOf(name)

    /**
     * Gives a user preset a new name, in the folder it was in. The store keys a preset's file
     * (and its MilkDrop file) by name, so a rename is a save under the new name and then a delete
     * of the old one, mirror copies included; the list changes at once so the grid does not show
     * the old name while the files move. Returns the name it was saved under, or null when there
     * was nothing to rename (a built-in, a name already gone).
     */
    fun renamePreset(
        from: String,
        to: String,
    ): String? {
        if (BuiltInPresets.isBuiltIn(from)) return null
        val name = PresetFiltering.cleanName(to)
        val preset =
            host.vizState.value.presets
                .firstOrNull { it.name == from && !BuiltInPresets.isBuiltIn(it.name) } ?: return null
        if (name.isEmpty() || name == from) return null
        val renamed = preset.copy(name = name)
        val folder = presets.folders.value.folderOf(from)
        host.updatePresets { list -> list.map { if (it.name == from) renamed else it } }
        storeScope.launch {
            val oldJson = presets.fileOf(from)?.name
            milkSourceOf(renamed, from)?.let { presets.materializeMilk(name, it) }
            presets.save(renamed, folder)
            removeMirroredPreset(oldJson, milkFileFor(from).name)
            presets.delete(from)
            mirrorPresetToChosenFolder(name)
            relistPresets()
        }
        return name
    }

    /**
     * A copy of [name] under the first free "<name> copy" name, in the same folder. A built-in's
     * copy becomes the user's own, which is how one gets renamed, moved or replaced at all.
     */
    fun duplicatePreset(name: String): String? {
        val all = host.vizState.value.presets
        val source = all.firstOrNull { it.name == name } ?: return null
        val copyName = PresetFiltering.uniqueName("$name copy", all.map { it.name })
        val copy = source.copy(name = copyName)
        val folder = if (BuiltInPresets.isBuiltIn(name)) "" else presets.folders.value.folderOf(name)
        host.updatePresets { it + copy }
        storeScope.launch {
            milkSourceOf(copy, name)?.let { presets.materializeMilk(copyName, it) }
            presets.save(copy, folder)
            mirrorPresetToChosenFolder(copyName)
            relistPresets()
        }
        return copyName
    }

    /**
     * The MilkDrop text a renamed or copied preset carries over. Older presets hold no text of
     * their own, only the file saved beside them under their old name, so that file is read.
     */
    private fun milkSourceOf(
        preset: Preset,
        oldName: String,
    ): String? {
        if (preset.sceneId != SceneIds.MILKDROP) return null
        return preset.milkPreset ?: runCatching { milkFileFor(oldName).takeIf { it.isFile }?.readText() }.getOrNull()
    }

    fun deletePreset(name: String) {
        if (BuiltInPresets.isBuiltIn(name)) return
        host.updatePresets { presets -> presets.filterNot { it.name == name } }
        storeScope.launch {
            removeMirroredPreset(presets.fileOf(name)?.name, milkFileFor(name).name)
            presets.delete(name)
            relistPresets()
        }
    }

    private fun removeMirroredPreset(
        jsonName: String?,
        milkName: String?,
    ) {
        val uriStr = host.presetMirrorUri ?: return
        scope.launch(Dispatchers.IO) {
            runCatching {
                val tree =
                    androidx.documentfile.provider.DocumentFile
                        .fromTreeUri(application, Uri.parse(uriStr))
                        ?: return@runCatching
                jsonName?.let { tree.findFile(it)?.delete() }
                milkName?.let { tree.findFile(it)?.delete() }
            }
        }
    }
}
