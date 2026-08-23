package dev.geode.data

import android.content.Context
import android.system.Os
import dev.geode.RingLog
import org.json.JSONObject
import java.io.File

/**
 * How a preset's expected texture name was satisfied, if it was.
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
 * it. [dev.geode.render.scene.MilkdropScene] puts that directory FIRST in the search paths, so a
 * per-preset choice always beats a same-named file in the shared folder — which is what makes a
 * deliberate substitution possible at all.
 *
 * Resolution ladder, most literal first:
 *  1. a persisted per-preset override (the person said "use this one");
 *  2. exact stem match, case-insensitive;
 *  3. normalized match — both sides lowercased, non-alphanumerics dropped, and the store side
 *     also compared with [TextureStore]'s `_hash` rename suffix stripped, so an import rename
 *     is undone rather than defeated;
 *  4. a stand-in: the closest-named texture, else the first one alphabetically. A preset with a
 *     wrong texture loads and is fixable from the UI; a preset with a missing sampler is a
 *     black frame with an error, which is strictly worse.
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

    /** The texture names [preset] references, excluding projectM's built-ins and `randNN`. */
    fun referencedTextures(preset: File): List<String> {
        val text = runCatching { preset.readText() }.getOrDefault("")
        if (text.isEmpty()) return emptyList()
        return SAMPLER_REFERENCE
            .findAll(text)
            .map { it.groupValues[1] }
            .distinctBy { it.lowercase() }
            .filterNot { it.lowercase() in BUILTIN_SAMPLERS }
            .filterNot { it.lowercase().startsWith("rand") }
            .toList()
    }

    /**
     * Resolve and materialize the links for one preset. Returns what was decided per name, in
     * the preset's own reference order, so the UI can show it verbatim.
     */
    fun relink(preset: File): List<MilkTextureLink> {
        val expected = referencedTextures(preset)
        val linkDir = linkDirFor(preset)
        // Rebuilt from scratch every time: a stale link to a removed texture, or one left by a
        // cleared override, is exactly the state this exists to prevent.
        linkDir.deleteRecursively()
        if (expected.isEmpty()) return emptyList()
        val stored = storedTextures()
        val overrides = overridesFor(preset.name)
        val links = expected.map { name -> resolve(name, stored, overrides) }
        if (links.any { it.texture != null }) linkDir.mkdirs()
        for (link in links) {
            val source = link.texture?.let { File(textureDir, it) } ?: continue
            materialize(source, linkDir, link.expected)
        }
        return links
    }

    /**
     * [relink] for every preset in the milk directory.
     *
     * Returns how many presets had to take a stand-in or have a texture missing outright -
     * renames undone and manual choices honoured are resolutions, not problems, so they do
     * not count.
     */
    fun relinkAll(): Int {
        val presets = milkDir.listFiles { f -> f.extension == "milk" }.orEmpty()
        // Link directories for presets that no longer exist are swept here rather than leaked:
        // this is the one place that sees the whole set.
        val live = presets.map { linkDirFor(it).name }.toSet()
        linksRoot
            .listFiles { f -> f.isDirectory && f.name !in live }
            .orEmpty()
            .forEach { it.deleteRecursively() }
        return presets.count { preset ->
            relink(preset).any { it.kind == MilkTextureLinkKind.SUBSTITUTED || it.kind == MilkTextureLinkKind.MISSING }
        }
    }

    /** What [relink] would decide right now, without touching the filesystem links. */
    fun resolutionFor(preset: File): List<MilkTextureLink> {
        val stored = storedTextures()
        val overrides = overridesFor(preset.name)
        return referencedTextures(preset).map { resolve(it, stored, overrides) }
    }

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
        runCatching { AtomicWrite.text(overridesFile, all.toString()) }
            .onFailure { RingLog.note(TAG, "override save failed", it) }
        return relink(preset)
    }

    // --- resolution -----------------------------------------------------------

    private fun resolve(
        expected: String,
        stored: List<File>,
        overrides: Map<String, String>,
    ): MilkTextureLink {
        overrides[expected.lowercase()]?.let { chosen ->
            // A pinned texture that has since been deleted falls through to the ladder rather
            // than resolving to a broken link that looks deliberate.
            if (stored.any { it.name == chosen }) {
                return MilkTextureLink(expected, chosen, MilkTextureLinkKind.OVERRIDDEN)
            }
        }
        if (stored.isEmpty()) return MilkTextureLink(expected, null, MilkTextureLinkKind.MISSING)

        stored.firstOrNull { it.nameWithoutExtension.equals(expected, ignoreCase = true) }?.let {
            return MilkTextureLink(expected, it.name, MilkTextureLinkKind.MATCHED)
        }
        val wanted = normalize(expected)
        stored.firstOrNull { normalize(stripRenameSuffix(it.nameWithoutExtension)) == wanted }?.let {
            return MilkTextureLink(expected, it.name, MilkTextureLinkKind.RENAMED)
        }
        val standIn =
            stored.maxWithOrNull(
                compareBy({ commonPrefix(normalize(it.nameWithoutExtension), wanted) }, { -it.name.length }),
            ) ?: stored.first()
        return MilkTextureLink(expected, standIn.name, MilkTextureLinkKind.SUBSTITUTED)
    }

    private fun storedTextures(): List<File> =
        textureDir
            .listFiles { f -> f.isFile && f.extension.lowercase() in TextureStore.IMAGE_EXTS }
            .orEmpty()
            .sortedBy { it.name.lowercase() }

    // --- materialization ------------------------------------------------------

    private fun materialize(
        source: File,
        linkDir: File,
        expected: String,
    ) {
        val names =
            buildSet {
                add("$expected.${source.extension}")
                add("${expected.lowercase()}.${source.extension.lowercase()}")
            }
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

    // --- overrides ------------------------------------------------------------

    private fun loadOverrides(): JSONObject = runCatching { JSONObject(overridesFile.readText()) }.getOrDefault(JSONObject())

    private fun overridesFor(presetFileName: String): Map<String, String> {
        val entry = loadOverrides().optJSONObject(presetFileName) ?: return emptyMap()
        return entry.keys().asSequence().associateWith { entry.optString(it) }.filterValues { it.isNotEmpty() }
    }

    internal companion object {
        const val TAG = "MilkTextureLinks"

        /** Lives inside the textures dir, but hidden from [TextureStore.list] by being a directory. */
        const val LINKS_DIR_NAME = ".links"

        /**
         * `sampler_foo`, `sampler_fw_foo` and friends. The optional group is the filtering mode
         * MilkDrop lets a preset choose per sampler; the trailing group is the texture name.
         */
        val SAMPLER_REFERENCE = Regex("""\bsampler(?:_(?:fw|fc|pw|pc))?_(\w+)""")

        val BUILTIN_SAMPLERS =
            setOf(
                "main", "blur1", "blur2", "blur3",
                "noise_lq", "noise_lq_lite", "noise_mq", "noise_hq",
                "noisevol_lq", "noisevol_hq",
            )

        fun normalize(name: String): String = name.lowercase().filter { it.isLetterOrDigit() }

        /**
         * Undo [TextureStore.safeTextureFileName]'s rename: when sanitization changed a name it
         * appended `_` plus eight hex characters of the original's hash, so the store-side stem
         * is compared with that suffix removed as well.
         */
        fun stripRenameSuffix(stem: String): String = stem.replace(Regex("_[0-9a-f]{8}$"), "")

        fun commonPrefix(
            a: String,
            b: String,
        ): Int = a.zip(b).takeWhile { (x, y) -> x == y }.count()
    }
}
