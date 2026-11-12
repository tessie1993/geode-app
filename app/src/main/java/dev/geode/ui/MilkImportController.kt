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

    @Suppress("TooGenericExceptionCaught")
    fun milkPresetFilesAsync(onDone: (List<MilkFile>) -> Unit) {
        scope.launch(Dispatchers.IO) {
            val files =
                try {
                    builtInDir().deleteRecursively()
                    File(importDir(), "textures").mkdirs()
                    MilkStarterPack.install(application, importDir())
                    // Idempotent refresh: catches textures imported before a preset existed,
                    // presets from older installs that predate linking, and starter presets.
                    textureLinks.relinkAll()
                    importDir()
                        .listFiles { f -> f.extension == "milk" }
                        .orEmpty()
                        .map { MilkFile(it.nameWithoutExtension, it.absolutePath) }
                        .sortedBy { it.name }
                } catch (t: Throwable) {
                    RingLog.note("MilkFiles", "milk list failed", t)
                    emptyList()
                }
            withContext(Dispatchers.Main) { onDone(files) }
        }
    }

    fun importMilkPresetAsync(
        uri: Uri,
        onDone: (String?) -> Unit,
    ) {
        scope.launch(Dispatchers.IO) {
            val path = importMilkPresetBlocking(uri)
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
