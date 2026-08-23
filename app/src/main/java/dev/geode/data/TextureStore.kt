package dev.geode.data

import android.content.Context
import android.graphics.BitmapFactory
import android.net.Uri
import java.io.File
import java.security.MessageDigest

data class MilkTexture(
    val name: String,
    val path: String,
)

data class TextureImportResult(
    val name: String,
    val storedName: String?,
    val skipReason: String?,
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
) {
    val removedGeneratedPresetPath: String? get() = removedGeneratedPresetPaths.firstOrNull()
}

class TextureStore(
    context: Context,
) {
    private val appContext = context.applicationContext
    private val dir = File(context.filesDir, "milk/textures").apply { mkdirs() }

    fun list(): List<MilkTexture> =
        dir
            .listFiles { f -> f.isFile && f.extension.lowercase() in IMAGE_EXTS }
            ?.sortedBy { it.name.lowercase() }
            ?.map { MilkTexture(it.name, it.absolutePath) }
            .orEmpty()

    fun import(uris: List<Uri>): List<MilkTexture> = importDetailed(uris).textures

    fun importDetailed(uris: List<Uri>): TextureImportOutcome = TextureImportOutcome(uris.map(::importOne), list())

    private fun importOne(uri: Uri): TextureImportResult {
        val name = displayName(uri) ?: "texture_${System.currentTimeMillis()}.png"
        val skipped = { reason: String -> TextureImportResult(name, null, reason) }
        val ext = name.substringAfterLast('.', "").lowercase()
        if (ext !in IMAGE_EXTS) {
            return skipped("not a supported image type (" + IMAGE_EXTS.sorted().joinToString(", ") + ")")
        }
        val tooLarge = "larger than ${MAX_TEXTURE_BYTES / (1024 * 1024)} MB"
        if ((declaredSize(uri) ?: 0L) > MAX_TEXTURE_BYTES) return skipped(tooLarge)
        val bytes = readBounded(uri) ?: return skipped("could not be read, or is $tooLarge")
        if (bytes.isEmpty()) return skipped("file is empty")
        validateImage(bytes, ext)?.let { return skipped(it) }
        val storedName = safeTextureFileName(name)
        val ok = runCatching { AtomicWrite.stream(File(dir, storedName)) { out -> out.write(bytes) } }.getOrDefault(false)
        return if (ok) TextureImportResult(name, storedName, null) else skipped("could not be written")
    }

    private fun validateImage(
        bytes: ByteArray,
        ext: String,
    ): String? =
        when (ext) {
            "dds" ->
                if (bytes.size >= 4 && bytes[0] == 'D'.code.toByte() && bytes[1] == 'D'.code.toByte() &&
                    bytes[2] == 'S'.code.toByte() && bytes[3] == ' '.code.toByte()
                ) {
                    null
                } else {
                    "not a DDS texture (missing DDS header)"
                }
            "tga" -> if (isTgaHeader(bytes)) null else "not a TGA image (unrecognized header)"
            else -> {
                val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                runCatching { BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts) }
                if (opts.outWidth > 0 && opts.outHeight > 0) null else "not a decodable image"
            }
        }

    private fun isTgaHeader(b: ByteArray): Boolean {
        if (b.size < 18) return false
        val colorMapType = b[1].toInt() and 0xff
        val imageType = b[2].toInt() and 0xff
        val pixelDepth = b[16].toInt() and 0xff
        return colorMapType <= 1 && imageType in TGA_IMAGE_TYPES && pixelDepth in TGA_PIXEL_DEPTHS
    }

    fun remove(name: String): List<MilkTexture> = removeDetailed(name).textures

    fun removeDetailed(name: String): TextureRemoveOutcome {
        val removed = runCatching { File(dir, name).delete() }.getOrDefault(false)
        val generated = generatedPresetFile(name)
        val remaining = list()
        val stem = name.substringBeforeLast('.')
        val legacy = File(generatedDir(), "show_$stem.milk")
        val sweepLegacy = legacy != generated && remaining.none { it.name.substringBeforeLast('.') == stem }
        val gone =
            listOfNotNull(
                generated.takeIf { runCatching { it.isFile && it.delete() }.getOrDefault(false) },
                legacy.takeIf { sweepLegacy && runCatching { it.isFile && it.delete() }.getOrDefault(false) },
            )
        return TextureRemoveOutcome(
            removed = removed,
            removedGeneratedPresetPaths = gone.map { it.absolutePath },
            textures = remaining,
        )
    }

    /**
     * The picked file, or null if it cannot be read or runs past [MAX_TEXTURE_BYTES].
     *
     * A texture arrives from the system picker, so its size is whatever the user chose -
     * and DDS masters in MilkDrop packs run to tens of megabytes. Reading one straight
     * into a ByteArray put that number in the app's heap unbounded; this stops at the cap
     * instead, which is why it does not use readBytes().
     */
    private fun readBounded(uri: Uri): ByteArray? =
        runCatching {
            appContext.contentResolver.openInputStream(uri)?.use { input ->
                val out = java.io.ByteArrayOutputStream()
                val buffer = ByteArray(READ_BUFFER_BYTES)
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    out.write(buffer, 0, n)
                    if (out.size() > MAX_TEXTURE_BYTES) return@use null
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
        runCatching {
            appContext.contentResolver
                .query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
        }.getOrNull() ?: uri.lastPathSegment?.substringAfterLast('/')

    private fun generatedDir(): File = File(appContext.filesDir, "milk/generated")

    private fun generatedPresetFile(name: String): File = File(generatedDir(), "show_${name.replace('.', '_')}.milk")

    fun generateDisplayPreset(textureName: String): String {
        val base = textureName.substringBeforeLast('.')
        generatedDir().mkdirs()
        val file = generatedPresetFile(textureName)
        AtomicWrite.text(
            file,
            """
            MILKDROP_PRESET_VERSION=201
            PSVERSION=2
            PSVERSION_WARP=2
            PSVERSION_COMP=2
            [preset00]
            fRating=3.0
            fGammaAdj=1.6
            fDecay=0.97
            fVideoEchoZoom=1.0
            fVideoEchoAlpha=0.0
            nWaveMode=7
            bAdditiveWaves=0
            fWaveAlpha=0.001
            zoom=1.0
            rot=0.0
            cx=0.5
            cy=0.5
            warp=0.0
            sx=1.0
            sy=1.0
            ob_size=0.0
            ib_size=0.0
            per_frame_1=wave_a = 0;
            comp_1=`sampler sampler_$base;
            comp_2=`shader_body
            comp_3=`{
            comp_4=`   float2 uv2 = (uv - 0.5) / (1.0 + 0.10 * bass) + 0.5;
            comp_5=`   float3 img = tex2D(sampler_$base, uv2).xyz;
            comp_6=`   float pulse = 0.75 + 0.45 * treb + 0.30 * bass;
            comp_7=`   ret = img * pulse;
            comp_8=`}
            """.trimIndent(),
        )
        return file.absolutePath
    }

    internal companion object {
        val IMAGE_EXTS = setOf("png", "jpg", "jpeg", "bmp", "tga", "dds", "dib")

        /** Comfortably above any real MilkDrop texture, far below what exhausts the heap. */
        private const val MAX_TEXTURE_BYTES = 16 * 1024 * 1024

        private const val READ_BUFFER_BYTES = 64 * 1024

        private val TGA_IMAGE_TYPES = setOf(1, 2, 3, 9, 10, 11)

        private val TGA_PIXEL_DEPTHS = setOf(8, 15, 16, 24, 32)

        internal fun safeTextureFileName(name: String): String {
            val rawBase = name.substringBeforeLast('.')
            val ext = name.substringAfterLast('.', "").lowercase()
            val base = rawBase.replace(Regex("[^A-Za-z0-9]"), "_")
            val safeBase = if (base.firstOrNull()?.isDigit() == true) "t$base" else base.ifEmpty { "tex" }
            if (safeBase == rawBase) return "$safeBase.$ext"
            val hash =
                MessageDigest
                    .getInstance("SHA-256")
                    .digest(rawBase.toByteArray(Charsets.UTF_8))
                    .take(4)
                    .joinToString("") { b -> "%02x".format(b.toInt() and 0xff) }
            return "${safeBase}_$hash.$ext"
        }
    }
}
