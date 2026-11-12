package dev.geode.ui

import android.app.Application
import android.content.ContentResolver
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import dev.geode.RingLog
import dev.geode.data.AtomicWrite
import dev.geode.data.MilkPackImporter
import dev.geode.data.MilkTextureLink
import dev.geode.data.MilkTextureLinks
import dev.geode.data.PresetStore
import dev.geode.data.ReadCap
import dev.geode.data.displayNameOf
import dev.geode.model.MilkFile
import dev.geode.render.scene.MilkStarterPack
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

internal class MilkImportController(
    private val application: Application,
    private val scope: CoroutineScope,
) {
    private val textureLinks = MilkTextureLinks(application)

    private fun builtInDir(): File = File(application.filesDir, "milk-builtin")

    private fun importDir(): File = File(application.filesDir, "milk")

    private val _files = MutableStateFlow<List<MilkFile>>(emptyList())

    /**
     * The user's .milk presets, newest first: the MilkDrop page's list, the order the stage's step walks
     * (native `core/viz/StageStep`) and Auto's pool. Re-read after every import here and whenever
     * [relist] is asked (a removed texture's generated preset), so neither the page nor Auto keeps a
     * list from before an import or a delete. Empty until the first read.
     */
    val files: StateFlow<List<MilkFile>> = _files

    /**
     * Reads the list again ([files]): the starter pack (a marker read once it is in) and the folder,
     * without [milkPresetFilesAsync]'s relink of every preset, so an import's relist stays cheap.
     */
    fun relist() = listAsync(relink = false) {}

    /** The list for Auto and randomize-everything, after a full relink; also published as [files]. */
    fun milkPresetFilesAsync(onDone: (List<MilkFile>) -> Unit) = listAsync(relink = true, onDone)

    @Suppress("TooGenericExceptionCaught")
    private fun listAsync(
        relink: Boolean,
        onDone: (List<MilkFile>) -> Unit,
    ) {
        scope.launch(Dispatchers.IO) {
            val listed =
                try {
                    if (relink) {
                        builtInDir().deleteRecursively()
                        File(importDir(), "textures").mkdirs()
                    }
                    MilkStarterPack.install(application, importDir())
                    // Idempotent refresh: catches textures imported before a preset existed,
                    // presets from older installs that predate linking, and starter presets.
                    if (relink) textureLinks.relinkAll()
                    importDir()
                        .listFiles { f -> f.isFile && f.extension == "milk" }
                        .orEmpty()
                        .sortedByDescending { it.lastModified() }
                        .map { MilkFile(it.nameWithoutExtension, it.absolutePath) }
                } catch (t: Throwable) {
                    RingLog.note("MilkFiles", "milk list failed", t)
                    emptyList()
                }
            withContext(Dispatchers.Main) {
                _files.value = listed
                onDone(listed)
            }
        }
    }

    fun importMilkPresetAsync(
        uri: Uri,
        onDone: (String?) -> Unit,
    ) {
        scope.launch(Dispatchers.IO) {
            val path = importMilkPresetBlocking(uri)
            if (path != null) relist()
            withContext(Dispatchers.Main) { onDone(path) }
        }
    }

    /**
     * A .milk another app (a file manager) asked us to open. The uri is untrusted: only a content or
     * file uri whose name ends in .milk is read, a file uri never from inside this app's own storage.
     * [offerOpenedMilkAsync] names it for the user's yes; only then [importOpenedMilkAsync] copies it
     * through the same capped, name-sanitised import as the picker's, never over an existing preset.
     */
    fun offerOpenedMilkAsync(
        uri: Uri,
        onDone: (String?) -> Unit,
    ) {
        scope.launch(Dispatchers.IO) {
            val name = openableMilkName(uri)
            withContext(Dispatchers.Main) { onDone(name) }
        }
    }

    fun importOpenedMilkAsync(
        uri: Uri,
        onDone: (String?) -> Unit,
    ) {
        scope.launch(Dispatchers.IO) {
            val path = if (openableMilkName(uri) != null) importMilkPresetBlocking(uri, keepExisting = true) else null
            if (path != null) relist()
            withContext(Dispatchers.Main) { onDone(path) }
        }
    }

    private fun openableMilkName(uri: Uri): String? {
        val name = runCatching { displayNameOf(uri) }.getOrNull() ?: return null
        if (!name.endsWith(".milk", ignoreCase = true)) return null
        val openable =
            when (uri.scheme) {
                ContentResolver.SCHEME_CONTENT -> true
                ContentResolver.SCHEME_FILE -> {
                    val path = uri.path ?: return null
                    val own = File(application.applicationInfo.dataDir).canonicalPath
                    val target = runCatching { File(path).canonicalPath }.getOrNull() ?: return null
                    target != own && !target.startsWith("$own/")
                }
                else -> false
            }
        return name.takeIf { openable }
    }

    @Suppress("TooGenericExceptionCaught")
    internal fun importMilkPresetBlocking(
        uri: Uri,
        keepExisting: Boolean = false,
    ): String? =
        try {
            val dir = importDir().apply { mkdirs() }
            val display = displayNameOf(uri).orEmpty().ifBlank { "preset" }
            val stem = PresetStore.milkFileName(display).removeSuffix(".milk")
            val file =
                generateSequence(1) { it + 1 }
                    .map { n -> File(dir, if (n == 1) "$stem.milk" else "$stem $n.milk") }
                    .first { !keepExisting || !it.exists() }
            // A .milk is text, tens of KB; anything past the cap is not one, and the write is dropped.
            val written =
                application.contentResolver.openInputStream(uri)?.use { input ->
                    AtomicWrite.stream(file) { out -> ReadCap.MILK.copy(input, out) }
                } ?: false
            if (written) textureLinks.relink(file)
            if (written) file.absolutePath else null
        } catch (t: Throwable) {
            RingLog.note("MilkImport", "milk import failed", t)
            null
        }

    fun importMilkFolderAsync(
        treeUri: Uri,
        onDone: (MilkPackImporter.Report) -> Unit,
    ) {
        scope.launch(Dispatchers.IO) {
            val entries = mutableListOf<MilkPackImporter.Entry>()
            runCatching {
                val root = DocumentFile.fromTreeUri(application, treeUri)
                if (root != null) collectMilkEntries(root, entries, depth = 0)
            }
            val imported = MilkPackImporter.import(entries, importDir())
            // The importer's missing count is measured BEFORE linking; what the person needs to
            // hear is the state after it - how many presets still had to take a stand-in.
            val report = imported.copy(presetsMissingTextures = textureLinks.relinkAll())
            relist()
            withContext(Dispatchers.Main) { onDone(report) }
        }
    }

    private fun collectMilkEntries(
        dir: DocumentFile,
        out: MutableList<MilkPackImporter.Entry>,
        depth: Int,
    ) {
        if (depth > MILK_WALK_DEPTH) return
        for (child in dir.listFiles()) {
            when {
                child.isDirectory -> collectMilkEntries(child, out, depth + 1)
                child.isFile -> {
                    val name = child.name ?: continue
                    val uri = child.uri
                    out +=
                        MilkPackImporter.Entry(name) {
                            runCatching { application.contentResolver.openInputStream(uri) }.getOrNull()
                        }
                }
            }
        }
    }

    /** What [MilkTextureLinks.relink] decided for this preset, in reference order. */
    fun textureLinksFor(path: String): List<MilkTextureLink> = textureLinks.resolutionFor(File(path))

    fun assignTextureAsync(
        path: String,
        expected: String,
        texture: String?,
        onDone: (List<MilkTextureLink>) -> Unit,
    ) {
        scope.launch(Dispatchers.IO) {
            val links = textureLinks.assign(File(path), expected, texture)
            withContext(Dispatchers.Main) { onDone(links) }
        }
    }

    private fun displayNameOf(uri: Uri): String? =
        application.contentResolver.displayNameOf(uri) ?: uri.lastPathSegment?.substringAfterLast('/')

    private companion object {
        const val MILK_WALK_DEPTH = 4
    }
}
