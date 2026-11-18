package dev.geode.data

import android.content.Context
import android.graphics.BitmapFactory
import android.net.Uri
import dev.geode.engine.bridge.MilkNative
import java.io.File

data class MilkTexture(
    val name: String,
    val path: String,
)

data class TextureImportResult(
    val name: String,
    val storedName: String?,
) {
    val imported: Boolean get() = storedName != null
}

data class TextureImportOutcome(
    val results: List<TextureImportResult>,
    val textures: List<MilkTexture>,
)

data class TextureRemoveOutcome(
    val removed: Boolean,
    val removedGeneratedPresetPaths: List<String>,
    val textures: List<MilkTexture>,
)

/**
 * The MilkDrop texture folder's shell: it reads what the picker hands over and writes and deletes the files.
 * Which pick is a texture, the name it is stored under and the preset that shows one are native
 * (core/viz/MilkTextures).
 */
class TextureStore(
    context: Context,
) {
    private val appContext = context.applicationContext
    private val dir = File(context.filesDir, "milk/textures").apply { mkdirs() }

    fun list(): List<MilkTexture> = MilkNative.milkTextureList(dir.path).map { MilkTexture(it, File(dir, it).absolutePath) }

    fun import(uris: List<Uri>): List<MilkTexture> = importDetailed(uris).textures

    fun importDetailed(uris: List<Uri>): TextureImportOutcome = TextureImportOutcome(uris.map(::importOne), list())

    private fun importOne(uri: Uri): TextureImportResult {
        val name = displayName(uri) ?: "texture_${System.currentTimeMillis()}.png"
        val skipped = TextureImportResult(name, null)
        if (!MilkNative.milkTextureAdmits(name, declaredSize(uri) ?: 0L)) return skipped
        // Null when it could not be read, or ran past the cap while reading.
        val bytes = readBounded(uri) ?: return skipped
        // Native judges a DDS or TGA by its header and any other picture by the size this decode reads.
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        runCatching { BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds) }
        val storedName = MilkNative.milkTextureStoredName(name, bytes, bounds.outWidth, bounds.outHeight) ?: return skipped
        val ok = runCatching { AtomicWrite.stream(File(dir, storedName)) { out -> out.write(bytes) } }.getOrDefault(false)
        return if (ok) TextureImportResult(name, storedName) else skipped
    }

    fun remove(name: String): List<MilkTexture> = removeDetailed(name).textures

    fun removeDetailed(name: String): TextureRemoveOutcome {
        val removed = runCatching { File(dir, name).delete() }.getOrDefault(false)
        val gone =
            MilkNative
                .milkTextureRemovedPresets(name, dir.path)
                .map { File(generatedDir(), it) }
                .filter { runCatching { it.isFile && it.delete() }.getOrDefault(false) }
        return TextureRemoveOutcome(
            removed = removed,
            removedGeneratedPresetPaths = gone.map { it.absolutePath },
            textures = list(),
        )
    }

    /**
     * The picked file, or null if it cannot be read or runs past the cap.
     *
     * A texture arrives from the system picker, so its size is whatever the user chose -
     * and DDS masters in MilkDrop packs run to tens of megabytes. Reading one straight
     * into a ByteArray put that number in the app's heap unbounded; this stops at the cap
     * instead, which is why it does not use readBytes().
     */
    private fun readBounded(uri: Uri): ByteArray? =
        runCatching {
            val cap = MilkNative.milkTextureMaxBytes()
            appContext.contentResolver.openInputStream(uri)?.use { input ->
                val out = java.io.ByteArrayOutputStream()
                val buffer = ByteArray(READ_BUFFER_BYTES)
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    out.write(buffer, 0, n)
                    if (out.size() > cap) return@use null
                }
                out.toByteArray()
            }
        }.getOrNull()

    /** What the provider claims the file weighs, so an oversized pick is refused before it is read. */
    private fun declaredSize(uri: Uri): Long? =
        runCatching {
            appContext.contentResolver
                .query(uri, arrayOf(android.provider.OpenableColumns.SIZE), null, null, null)
                ?.use { c -> if (c.moveToFirst() && !c.isNull(0)) c.getLong(0) else null }
        }.getOrNull()

    private fun displayName(uri: Uri): String? =
        appContext.contentResolver.displayNameOf(uri) ?: uri.lastPathSegment?.substringAfterLast('/')

    private fun generatedDir(): File = File(appContext.filesDir, "milk/generated")

    fun generateDisplayPreset(textureName: String): String {
        generatedDir().mkdirs()
        val file = File(generatedDir(), MilkNative.milkTextureShowPresetName(textureName))
        AtomicWrite.text(file, MilkNative.milkTextureShowPresetText(textureName))
        return file.absolutePath
    }

    private companion object {
        const val READ_BUFFER_BYTES = 64 * 1024
    }
}
