package dev.geode.data

import java.io.File
import java.io.InputStream

object MilkPackImporter {
    class Entry(
        val name: String,
        val open: () -> InputStream?,
    )

    data class Report(
        val presets: Int,
        val textures: Int,
        val skipped: Int,
        val presetsMissingTextures: Int,
    ) {
        val total: Int get() = presets + textures
    }

    private val TEXTURE_EXTENSIONS = setOf("png", "jpg", "jpeg", "bmp", "tga", "dds")

    fun import(
        entries: List<Entry>,
        milkDir: File,
    ): Report {
        val textureDir = File(milkDir, "textures")
        milkDir.mkdirs()
        textureDir.mkdirs()
        var presets = 0
        var textures = 0
        var skipped = 0
        val importedPresets = mutableListOf<File>()
        for (entry in entries) {
            val extension = entry.name.substringAfterLast('.', "").lowercase()
            val target = targetFor(entry.name, extension, milkDir, textureDir) ?: continue
            val written = if (target.exists()) false else copy(entry, target)
            when {
                !written -> skipped++
                extension == "milk" -> {
                    presets++
                    importedPresets += target
                }
                else -> textures++
            }
        }
        return Report(
            presets = presets,
            textures = textures,
            skipped = skipped,
            presetsMissingTextures = importedPresets.count { missesATexture(it, textureDir) },
        )
    }

    private fun targetFor(
        name: String,
        extension: String,
        milkDir: File,
        textureDir: File,
    ): File? =
        when {
            extension == "milk" -> File(milkDir, PresetStore.milkFileName(name))
            extension in TEXTURE_EXTENSIONS -> File(textureDir, name.substringAfterLast('/'))
            else -> null
        }

    private fun copy(
        entry: Entry,
        target: File,
    ): Boolean =
        runCatching {
            entry.open()?.use { input -> AtomicWrite.stream(target) { out -> input.copyTo(out) } } ?: false
        }.getOrDefault(false)

    fun missesATexture(
        preset: File,
        textureDir: File,
    ): Boolean {
        val text = runCatching { preset.readText() }.getOrDefault("")
        if (text.isEmpty()) return false
        val available =
            textureDir
                .listFiles()
                .orEmpty()
                .map { it.nameWithoutExtension.lowercase() }
                .toSet()
        // The sampler grammar lives in MilkTextureLinks so the importer and the linker can
        // never drift apart on what counts as a texture reference.
        return MilkTextureLinks.SAMPLER_REFERENCE
            .findAll(text)
            .map { it.groupValues[1].lowercase() }
            .filterNot { it in MilkTextureLinks.BUILTIN_SAMPLERS }
            .filterNot { it.startsWith("rand") }
            .any { it !in available }
    }
}
