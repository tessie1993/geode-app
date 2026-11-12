package dev.geode.data

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.annotation.WorkerThread
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import java.io.File

/** A preset as native read it (core/viz/LookFiles): what the screens list and a look applies. */
data class Preset(
    val name: String,
    val sceneId: String,
    val attack: Float,
    val decay: Float,
    val customShader: String? = null,
    val params: SceneParams = SceneParams.DEFAULT,
    val milkPreset: String? = null,
)

/**
 * The preset folders (files/presets and its sub-folders, with each preset's .milk source in files/milk): bytes and
 * folders only. A preset's text both ways, every file name and the list's order are native (core/viz/LookFiles).
 */
class PresetStore(
    context: Context,
) {
    private val dir = File(context.filesDir, "presets").apply { mkdirs() }

    private val milkDir = File(context.filesDir, "milk")

    private var migrated = false

    /**
     * Renames files saved under older name rules, once, before the first read or write of the folder. It reads every
     * preset, so it runs on whichever thread first uses the store (the session's IO listing at start), not in the
     * constructor, which the session runs on the main thread.
     */
    @Synchronized
    private fun ensureMigrated() {
        if (migrated) return
        migrated = true
        jsonFiles().forEach { f ->
            val stem = runCatching { safeFileName(fromJson(f.readText()).name) }.getOrNull() ?: return@forEach
            val target = File(f.parentFile, "$stem.json")
            if (f.nameWithoutExtension == stem || target.exists() || !f.renameTo(target)) return@forEach
            val milk = File(milkDir, f.nameWithoutExtension + ".milk")
            val milkTarget = File(milkDir, "$stem.milk")
            if (milk.isFile && !milkTarget.exists()) milk.renameTo(milkTarget)
        }
    }

    fun folderOf(name: String): String {
        ensureMigrated()
        val folder = findFile(name)?.parentFile ?: return ""
        return folder.relativeTo(dir).path
    }

    @WorkerThread
    fun folders(): List<String> {
        ensureMigrated()
        val all =
            dir
                .walkTopDown()
                .filter { it.isDirectory && it != dir }
                .map { it.relativeTo(dir).path }
                .toList()
        return GeodeNative.looksOrder(all.toTypedArray(), false).map { all[it] }
    }

    fun addFolder(path: String) {
        ensureMigrated()
        File(dir, GeodeNative.fileSafeChars(path)).mkdirs()
    }

    fun renameFolder(
        from: String,
        to: String,
    ) {
        ensureMigrated()
        val src = File(dir, GeodeNative.fileSafeChars(from))
        if (src.isDirectory) src.renameTo(File(dir, GeodeNative.fileSafeChars(to)))
    }

    @WorkerThread
    fun moveToFolder(
        name: String,
        folder: String,
    ) {
        ensureMigrated()
        val f = findFile(name) ?: return
        f.renameTo(File(folderDir(folder), f.name))
    }

    fun fileOf(name: String): File? {
        ensureMigrated()
        return findFile(name)
    }

    fun milkFileOf(presetName: String): File = File(milkDir, milkFileName(presetName))

    @WorkerThread
    fun materializeMilk(
        presetName: String,
        source: String?,
    ): String? {
        ensureMigrated()
        val file = milkFileOf(presetName)
        if (source != null && runCatching { !file.isFile || file.readText() != source }.getOrDefault(true)) AtomicWrite.text(file, source)
        return file.takeIf { it.isFile }?.absolutePath
    }

    @WorkerThread
    fun list(): List<Preset> {
        ensureMigrated()
        val read =
            jsonFiles().mapNotNull { f ->
                runCatching { fromJson(f.readText()) }
                    .onFailure { dev.geode.RingLog.note("PresetStore", "unreadable preset skipped: ${f.name}", it) }
                    .getOrNull()
            }
        return GeodeNative.looksOrder(read.map { it.name }.toTypedArray(), false).map { read[it] }
    }

    @WorkerThread
    fun save(
        preset: Preset,
        folder: String = "",
    ) {
        ensureMigrated()
        val dest = File(folderDir(folder), safeFileName(preset.name) + ".json")
        val previous = findFile(preset.name)?.takeIf { it != dest }
        if (AtomicWrite.text(dest, toJson(preset))) previous?.delete()
    }

    @WorkerThread
    fun delete(name: String) {
        ensureMigrated()
        findFile(name)?.delete()
    }

    private fun jsonFiles(): List<File> = dir.walkTopDown().filter { it.isFile && it.extension == "json" }.toList()

    private fun findFile(name: String): File? {
        val stem = safeFileName(name)
        return jsonFiles().firstOrNull { it.nameWithoutExtension == stem }
    }

    private fun folderDir(folder: String): File =
        if (folder.isEmpty()) dir else File(dir, GeodeNative.fileSafeChars(folder)).apply { mkdirs() }

    companion object {
        fun toJson(p: Preset): String =
            checkNotNull(
                GeodeNative.presetWrite(
                    p.name,
                    p.sceneId,
                    p.attack,
                    p.decay,
                    p.customShader,
                    p.milkPreset,
                    p.params.frame(),
                    p.params.customPaletteId,
                ),
            )

        /** A preset's text as native reads it; throws when it is no preset (callers skip it). */
        fun fromJson(json: String): Preset {
            val frame = FloatArray(SceneParams.FIELDS)
            val strings = arrayOfNulls<String>(5)
            val envelope = FloatArray(2)
            require(GeodeNative.presetRead(json, frame, strings, envelope)) { "not a preset" }
            return Preset(
                name = strings[0].orEmpty(),
                sceneId = strings[1].orEmpty(),
                attack = envelope[0],
                decay = envelope[1],
                customShader = strings[2],
                params = SceneParams.read(frame, strings[4].orEmpty()),
                milkPreset = strings[3],
            )
        }

        fun milkFileName(presetName: String): String = GeodeNative.presetMilkFileName(presetName)

        fun safeFileName(name: String): String = fileStem(name)

        /** Native's file stem rule (`geode_file_safe_name`); a seam for JVM tests of the other stores, which have no libgeode. */
        @VisibleForTesting
        internal var fileStem: (String) -> String = { GeodeNative.fileSafeName(it) }
    }
}

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
