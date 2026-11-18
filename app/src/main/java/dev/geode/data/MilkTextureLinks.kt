package dev.geode.data

import android.content.Context
import android.system.Os
import dev.geode.RingLog
import dev.geode.engine.bridge.MilkNative
import org.json.JSONObject
import java.io.File

/**
 * How a preset's expected texture name was satisfied, if it was. The order is native's
 * (`core/viz/MilkTextures` LinkKind), which answers with the ordinal.
 */
enum class MilkTextureLinkKind {
    /** The store holds this texture under the name the preset expects. */
    MATCHED,

    /** Matched through name normalization — the import renamed it, the preset did not know. */
    RENAMED,

    /** The person chose this texture for this name, and the choice is persisted. */
    OVERRIDDEN,

    /** Nothing matched, so a stand-in was linked to let the preset load anyway. */
    SUBSTITUTED,

    /** No textures exist at all; nothing could be linked. */
    MISSING,
}

data class MilkTextureLink(
    /** The name the preset references, e.g. `headlights` from `sampler_headlights`. */
    val expected: String,
    /** The stored texture file satisfying it, or null when [kind] is [MilkTextureLinkKind.MISSING]. */
    val texture: String?,
    val kind: MilkTextureLinkKind,
)

/**
 * The bridge between what a `.milk` preset asks for and what the texture store holds.
 *
 * WHY: projectM resolves a preset's `sampler_foo` by looking for a file whose stem is exactly
 * `foo` in its search paths — no fuzzy matching, no substitution. Meanwhile [TextureStore]
 * sanitizes imported names (`my tex.png` becomes `my_tex_<hash>.png`) so the file a person
 * imported FOR a preset routinely stops matching the name that preset expects, and packs are
 * assembled by different people than their textures. The observable failure is the reported one:
 * a preset that "won't load without the texture", sitting next to the very texture it wants.
 *
 * The fix is not to teach projectM to search better — it is upstream code — but to materialize
 * the resolution as a directory of links projectM can already read: for each expected name,
 * `milk/textures/.links/<presetStem>/<expected>.<ext>` points at the stored file that satisfies
 * it. The MilkDrop scene puts that directory FIRST in the search paths, so a per-preset choice
 * always beats a same-named file in the shared folder — which is what makes a deliberate
 * substitution possible at all.
 *
 * The resolution itself (override, exact, the store's rename undone, a stand-in) is native,
 * `core/viz/MilkTextures`; this class keeps the person's choices and makes the links.
 *
 * Links are symlinks where the filesystem allows (app-private storage is ext4/f2fs, so it does)
 * and copies where it refuses. Everything is idempotent: [relink] rebuilds a preset's link
 * directory from scratch each time, so texture imports, removals and override changes are all
 * handled by simply running it again.
 */
class MilkTextureLinks(
    context: Context,
) {
    private val appContext = context.applicationContext
    private val milkDir = File(appContext.filesDir, "milk")
    private val textureDir = File(milkDir, "textures")
    private val linksRoot = File(textureDir, LINKS_DIR_NAME)
    private val overridesFile = File(milkDir, "texture-links.json")

    /** Where a pinned preset can be: the user's folder, and the texture show presets (`TextureStore`). */
    private val presetDirs = listOf(milkDir, File(milkDir, "generated"))

    /** Native's answer for one preset: the links, the file names each is made under, and whether the person must look. */
    private class Resolution(
        val needsYou: Boolean,
        val links: List<MilkTextureLink>,
        val linkNames: List<List<String>>,
    )

    /**
     * Resolve and materialize the links for one preset. Returns what was decided per name, in
     * the preset's own reference order, so the UI can show it verbatim.
     */
    fun relink(preset: File): List<MilkTextureLink> = relinked(preset).links

    /**
     * [relink] for every preset in the milk directory.
     *
     * Returns how many presets had to take a stand-in or have a texture missing outright -
     * renames undone and manual choices honoured are resolutions, not problems, so they do
     * not count.
     */
    fun relinkAll(): Int {
        // Link directories for presets that no longer exist are swept here rather than leaked:
        // this is the one place that sees the whole set.
        MilkNative.milkTextureStaleLinkDirs(milkDir.path).forEach { File(linksRoot, it).deleteRecursively() }
        // Read once for the pass; pins for presets that are gone (deleted, renamed) go here too.
        val overrides = loadOverrides()
        if (dropDeadPins(overrides) { name -> presetDirs.any { File(it, name).isFile } }) saveOverrides(overrides)
        return MilkNative.milkTextureLinkedPresets(milkDir.path).count { relinked(File(milkDir, it), overrides).needsYou }
    }

    /** What [relink] would decide right now, without touching the filesystem links. */
    fun resolutionFor(preset: File): List<MilkTextureLink> = resolution(preset).links

    /**
     * Pin [textureName] (a stored file name) to [expected] for this preset, or clear the pin
     * with null, then rebuild the links so the choice is live on the next preset load.
     */
    fun assign(
        preset: File,
        expected: String,
        textureName: String?,
    ): List<MilkTextureLink> {
        val all = loadOverrides()
        val forPreset = all.optJSONObject(preset.name) ?: JSONObject()
        if (textureName == null) forPreset.remove(expected.lowercase()) else forPreset.put(expected.lowercase(), textureName)
        if (forPreset.length() == 0) all.remove(preset.name) else all.put(preset.name, forPreset)
        saveOverrides(all)
        return relink(preset)
    }

    private fun saveOverrides(all: JSONObject) {
        runCatching { AtomicWrite.text(overridesFile, all.toString()) }
            .onFailure { RingLog.note(TAG, "override save failed", it) }
    }

    private fun relinked(
        preset: File,
        overrides: JSONObject = loadOverrides(),
    ): Resolution {
        val linkDir = linkDirFor(preset)
        // Rebuilt from scratch every time: a stale link to a removed texture, or one left by a
        // cleared override, is exactly the state this exists to prevent.
        linkDir.deleteRecursively()
        val resolution = resolution(preset, overrides)
        resolution.links.forEachIndexed { i, link ->
            val texture = link.texture ?: return@forEachIndexed
            materialize(File(textureDir, texture), linkDir, resolution.linkNames[i])
        }
        return resolution
    }

    private fun resolution(
        preset: File,
        all: JSONObject = loadOverrides(),
    ): Resolution {
        val overrides = overridesFor(preset.name, all)
        val answer =
            MilkNative.milkTextureLinks(preset.path, textureDir.path, overrides.keys.toTypedArray(), overrides.values.toTypedArray())
        val rows = answer.drop(1).chunked(MilkNative.MILK_LINK_FIELDS).filter { it.size == MilkNative.MILK_LINK_FIELDS }
        return Resolution(
            needsYou = answer.firstOrNull() == "1",
            links =
                rows.map { (expected, texture, kind) ->
                    MilkTextureLink(
                        expected,
                        texture.ifEmpty { null },
                        MilkTextureLinkKind.entries.getOrElse(kind.toIntOrNull() ?: -1) { MilkTextureLinkKind.MISSING },
                    )
                },
            linkNames = rows.map { row -> row.drop(LINK_NAMES_AT).filter { it.isNotEmpty() } },
        )
    }

    private fun materialize(
        source: File,
        linkDir: File,
        names: List<String>,
    ) {
        linkDir.mkdirs()
        for (name in names) {
            val link = File(linkDir, name)
            val ok =
                runCatching { Os.symlink(source.absolutePath, link.absolutePath) }.isSuccess ||
                    // A filesystem that refuses symlinks gets a copy; slower to write once,
                    // identical to read.
                    runCatching { source.copyTo(link, overwrite = true) }.isSuccess
            if (!ok) RingLog.note(TAG, "could not link ${source.name} as $name")
        }
    }

    private fun linkDirFor(preset: File): File = File(linksRoot, preset.nameWithoutExtension)

    private fun loadOverrides(): JSONObject = runCatching { JSONObject(overridesFile.readText()) }.getOrDefault(JSONObject())

    private fun overridesFor(
        presetFileName: String,
        all: JSONObject,
    ): Map<String, String> {
        val entry = all.optJSONObject(presetFileName) ?: return emptyMap()
        return entry.keys().asSequence().associateWith { entry.optString(it) }
    }

    internal companion object {
        const val TAG = "MilkTextureLinks"

        /** A native row is expected, texture, kind, then the names the preset asks for. */
        const val LINK_NAMES_AT = 3

        /** Lives inside the textures dir, but hidden from [TextureStore.list] by being a directory. */
        const val LINKS_DIR_NAME = ".links"

        /** Removes from [overrides] the pins of every preset [exists] no longer finds; true when one went. */
        fun dropDeadPins(
            overrides: JSONObject,
            exists: (String) -> Boolean,
        ): Boolean {
            val dead =
                overrides
                    .keys()
                    .asSequence()
                    .filterNot(exists)
                    .toList()
            dead.forEach { overrides.remove(it) }
            return dead.isNotEmpty()
        }
    }
}
