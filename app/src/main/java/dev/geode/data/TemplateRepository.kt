// Storage, sharing and the bundled starter set for video templates.
//
// Mirrors PresetStore/PresetRepository deliberately: one file per template under
// filesDir, AtomicWrite for every write so a half-written file can never replace a
// good one, and a StateFlow-backed repository over a @WorkerThread store.
//
// Two rules here exist to protect the "nothing is ever silently dropped" guarantee
// that TemplateFormat.kt documents:
//
//   * A file this build cannot parse is SKIPPED, never deleted, quarantined or
//     rewritten. It may be a template from a newer version, or a good file this
//     build has a bug reading; either way the bytes stay exactly as the user left
//     them so a later build can pick them up.
//   * Import matches on TemplateId, not on name. Re-importing the same template
//     updates the one already in the library instead of breeding near-duplicates,
//     which is what makes the format tradeable rather than disposable.
//
// Sharing has two paths, both single-file by design: stageForShare() writes a
// nicely named copy into cacheDir for a normal file share (WhatsApp, mail, Drive),
// and shareLink() folds the same bytes into one geode://template/... link for
// pasting straight into a chat message.

package dev.geode.data

import android.content.Context
import androidx.annotation.WorkerThread
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.io.OutputStream

/** Outcome of writing a template out. Failure is a value, not an exception. */
sealed interface TemplateWrite {
    data object Written : TemplateWrite

    data class Failed(
        val why: String,
    ) : TemplateWrite
}

/** Outcome of bringing a template in from a file, a link or the starter set. */
sealed interface TemplateImport {
    data class Added(
        val template: VideoTemplate,
    ) : TemplateImport

    /** Same id as one already in the library: the newer copy wins. */
    data class Replaced(
        val template: VideoTemplate,
        val previousName: String,
    ) : TemplateImport

    data class Unreadable(
        val why: String,
    ) : TemplateImport

    data class WriteFailed(
        val why: String,
    ) : TemplateImport
}

/**
 * The on-disk template library: one `<name>--<id>.geode.json` per template.
 *
 * The id is part of the file name so a template can be found without reading every
 * file, and renaming a template rewrites under the new name and drops the old file,
 * exactly as PresetStore does.
 */
class TemplateStore(
    context: Context,
) {
    private val dir = File(context.filesDir, DIR_NAME).apply { mkdirs() }

    @WorkerThread
    fun list(): List<VideoTemplate> =
        dir
            .listFiles()
            .orEmpty()
            .filter { it.isFile && it.name.endsWith(TemplateFormat.FILE_SUFFIX) }
            .mapNotNull(::readOrSkip)
            .sortedBy { it.name.lowercase() }

    @WorkerThread
    fun find(id: TemplateId): VideoTemplate? = fileOf(id)?.let(::readOrSkip)

    fun fileOf(id: TemplateId): File? {
        val tail = ID_SEPARATOR + idTag(id) + TemplateFormat.FILE_SUFFIX
        return dir
            .listFiles()
            .orEmpty()
            .firstOrNull { it.isFile && it.name.endsWith(tail) }
    }

    @WorkerThread
    fun save(template: VideoTemplate): TemplateWrite {
        val destination = File(dir, storeFileNameFor(template))
        val previous = fileOf(template.id)?.takeIf { it.absolutePath != destination.absolutePath }
        if (!AtomicWrite.text(destination, TemplateFormat.encode(template))) {
            return TemplateWrite.Failed("could not save ${destination.name}")
        }
        previous?.delete()
        return TemplateWrite.Written
    }

    @WorkerThread
    fun delete(id: TemplateId): Boolean = fileOf(id)?.delete() ?: false

    private fun readOrSkip(file: File): VideoTemplate? {
        val text = runCatching { file.readText() }.getOrNull() ?: return null
        return when (val parse = TemplateFormat.decode(text)) {
            is TemplateParse.Parsed -> parse.template.copy(origin = TemplateOrigin.SAVED)
            is TemplateParse.NotATemplate -> skip(file, parse.why)
            is TemplateParse.Malformed -> skip(file, parse.why)
        }
    }

    private fun skip(
        file: File,
        why: String,
    ): VideoTemplate? {
        dev.geode.RingLog.note(TAG, "template kept but not loaded: ${file.name} ($why)")
        return null
    }

    private fun storeFileNameFor(template: VideoTemplate): String {
        val stem =
            PresetStore
                .safeFileName(template.name)
                .take(MAX_STEM_LENGTH)
                .trim()
                .ifBlank { FALLBACK_STEM }
        return stem + ID_SEPARATOR + idTag(template.id) + TemplateFormat.FILE_SUFFIX
    }

    private fun idTag(id: TemplateId): String = id.value.replace(UNSAFE_ID_CHARS, "_").take(MAX_ID_TAG_LENGTH)

    private companion object {
        const val DIR_NAME = "templates"
        const val ID_SEPARATOR = "--"
        const val FALLBACK_STEM = "Template"
        const val MAX_STEM_LENGTH = 60
        const val MAX_ID_TAG_LENGTH = 40
        const val TAG = "TemplateStore"

        val UNSAFE_ID_CHARS = Regex("[^A-Za-z0-9_-]")
    }
}

/**
 * The starter set, defined in code rather than in assets so it always parses and
 * always matches the current model. Browsing them costs no I/O; using one copies it
 * into the library under a fresh id (see [TemplateRepository.adopt]) so editing a
 * copy never silently changes what "Reel Neon" means.
 */
object BundledTemplates {
    val ALL: List<VideoTemplate> =
        listOf(
            reelNeon(),
            loFiLoop(),
            vinylSpin(),
            albumTeaser(),
            podcastBars(),
            master4K(),
        )

    fun isBundled(id: TemplateId): Boolean = ALL.any { it.id == id }

    private fun reelNeon(): VideoTemplate =
        starter(
            slug = "reel-neon",
            name = "Reel Neon",
            notes = "Vertical hook clip. Loud, loop-safe, thirty seconds around the biggest moment.",
            look =
                TemplateLook(
                    sceneId = SceneIds.FLUID,
                    attack = 0.85f,
                    decay = 0.24f,
                    params =
                        SceneParams(
                            speed = 1.3f,
                            audioDrive = 1.6f,
                            beatResponse = 1.9f,
                            pulse = 0.6f,
                            flash = 0.35f,
                            bloom = 0.6f,
                            contrast = 1.2f,
                            intensity = 1.15f,
                            bassGain = 1.4f,
                            palette = 1,
                            paramFadeSec = 0.4f,
                        ),
                ),
            layout =
                TemplateLayout(
                    ratio = Tolerant.Known(ExportRatio.R9_16),
                    artwork = Tolerant.Known(ArtworkStyle.BEHIND_BLURRED),
                    progress = Tolerant.Known(ProgressStyle.BAR),
                ),
            text = TemplateText(),
            export =
                TemplateExport(
                    fps = 30,
                    loopSafe = true,
                    segment = TemplateSegment.LoudestWindow(HOOK_30S),
                ),
        )

    private fun loFiLoop(): VideoTemplate =
        starter(
            slug = "lo-fi-loop",
            name = "Lo-Fi Loop",
            notes = "Square, slow and soft. Made to sit under a study playlist without shouting.",
            look =
                TemplateLook(
                    sceneId = SceneIds.AURORA,
                    attack = 0.4f,
                    decay = 0.06f,
                    params =
                        SceneParams(
                            speed = 0.5f,
                            audioDrive = 0.85f,
                            beatResponse = 0.45f,
                            trails = true,
                            trailLength = 0.8f,
                            saturation = 0.8f,
                            brightness = 0.9f,
                            colorCycle = true,
                            cycleSpeed = 0.025f,
                            palette = 3,
                            sway = 0.35f,
                            vignette = 0.4f,
                            grain = 0.18f,
                            paramFadeSec = 1.5f,
                        ),
                ),
            layout =
                TemplateLayout(
                    ratio = Tolerant.Known(ExportRatio.R1_1),
                    artwork = Tolerant.Known(ArtworkStyle.CARD),
                    artworkScale = 0.55f,
                    progress = Tolerant.Known(ProgressStyle.NONE),
                ),
            text =
                TemplateText(
                    slots =
                        listOf(
                            TextSlot(
                                role = Tolerant.Known(TextRole.TITLE),
                                anchor = Tolerant.Known(TextAnchor.BOTTOM_CENTER),
                                offsetY = -0.05f,
                                sizeSp = 26f,
                                weight = Tolerant.Known(TextWeight.MEDIUM),
                            ),
                            TextSlot(
                                role = Tolerant.Known(TextRole.ARTIST),
                                anchor = Tolerant.Known(TextAnchor.BOTTOM_CENTER),
                                sizeSp = 18f,
                                weight = Tolerant.Known(TextWeight.REGULAR),
                            ),
                        ),
                ),
            export =
                TemplateExport(
                    fps = 30,
                    loopSafe = true,
                    segment = TemplateSegment.LoudestWindow(HOOK_45S),
                ),
        )

    private fun vinylSpin(): VideoTemplate =
        starter(
            slug = "vinyl-spin",
            name = "Vinyl Spin",
            notes = "Round artwork, ring progress, whole track. The classic single-cover post.",
            look =
                TemplateLook(
                    sceneId = SceneIds.RING,
                    attack = 0.55f,
                    decay = 0.1f,
                    params =
                        SceneParams(
                            speed = 0.8f,
                            audioDrive = 1.1f,
                            beatResponse = 1.0f,
                            saturation = 1.1f,
                            bloom = 0.3f,
                            vignette = 0.5f,
                            palette = 5,
                            paramFadeSec = 0.8f,
                        ),
                ),
            layout =
                TemplateLayout(
                    ratio = Tolerant.Known(ExportRatio.R1_1),
                    artwork = Tolerant.Known(ArtworkStyle.CIRCLE),
                    artworkScale = 0.7f,
                    progress = Tolerant.Known(ProgressStyle.RING),
                ),
            text =
                TemplateText(
                    slots =
                        listOf(
                            TextSlot(
                                role = Tolerant.Known(TextRole.ARTIST),
                                pattern = "{artist}",
                                anchor = Tolerant.Known(TextAnchor.TOP_CENTER),
                                offsetY = 0.06f,
                                sizeSp = 20f,
                                allCaps = true,
                                weight = Tolerant.Known(TextWeight.BOLD),
                            ),
                        ),
                ),
            export =
                TemplateExport(
                    fps = 30,
                    loopSafe = false,
                    segment = TemplateSegment.WholeTrack,
                ),
        )

    private fun albumTeaser(): VideoTemplate =
        starter(
            slug = "album-teaser",
            name = "Album Teaser",
            notes = "Four-by-five feed post, fifteen seconds, album name front and centre.",
            look =
                TemplateLook(
                    sceneId = SceneIds.WAVES,
                    attack = 0.7f,
                    decay = 0.15f,
                    params =
                        SceneParams(
                            speed = 1.0f,
                            audioDrive = 1.25f,
                            beatResponse = 1.3f,
                            trails = true,
                            trailLength = 0.45f,
                            contrast = 1.1f,
                            bloom = 0.4f,
                            palette = 2,
                            paramFadeSec = 0.6f,
                        ),
                ),
            layout =
                TemplateLayout(
                    ratio = Tolerant.Known(ExportRatio.R4_5),
                    artwork = Tolerant.Known(ArtworkStyle.CARD),
                    artworkScale = 0.6f,
                    progress = Tolerant.Known(ProgressStyle.BAR),
                ),
            text =
                TemplateText(
                    slots =
                        listOf(
                            TextSlot(
                                role = Tolerant.Known(TextRole.ALBUM),
                                anchor = Tolerant.Known(TextAnchor.MIDDLE_CENTER),
                                sizeSp = 40f,
                                allCaps = true,
                                weight = Tolerant.Known(TextWeight.BOLD),
                            ),
                            TextSlot(
                                role = Tolerant.Known(TextRole.CAPTION),
                                pattern = "{artist} - {title}",
                                anchor = Tolerant.Known(TextAnchor.BOTTOM_CENTER),
                                offsetY = -0.07f,
                                sizeSp = 20f,
                            ),
                        ),
                ),
            export =
                TemplateExport(
                    fps = 30,
                    loopSafe = true,
                    segment = TemplateSegment.LoudestWindow(HOOK_15S),
                ),
        )

    private fun podcastBars(): VideoTemplate =
        starter(
            slug = "podcast-bars",
            name = "Podcast Bars",
            notes = "Wide, calm spectrum for speech. Whole episode, readable title, no strobing.",
            look =
                TemplateLook(
                    sceneId = SceneIds.BARS,
                    attack = 0.5f,
                    decay = 0.2f,
                    params =
                        SceneParams(
                            speed = 0.9f,
                            audioDrive = 1.0f,
                            beatResponse = 0.6f,
                            saturation = 0.7f,
                            brightness = 0.95f,
                            midGain = 1.25f,
                            vignette = 0.25f,
                            palette = 0,
                            paramFadeSec = 1.0f,
                        ),
                ),
            layout =
                TemplateLayout(
                    ratio = Tolerant.Known(ExportRatio.R16_9),
                    artwork = Tolerant.Known(ArtworkStyle.CARD),
                    artworkScale = 0.4f,
                    progress = Tolerant.Known(ProgressStyle.BAR),
                ),
            text =
                TemplateText(
                    slots =
                        listOf(
                            TextSlot(
                                role = Tolerant.Known(TextRole.TITLE),
                                anchor = Tolerant.Known(TextAnchor.MIDDLE_LEFT),
                                offsetX = 0.08f,
                                sizeSp = 32f,
                                weight = Tolerant.Known(TextWeight.BOLD),
                            ),
                            TextSlot(
                                role = Tolerant.Known(TextRole.CREDIT),
                                pattern = "{artist} - {index} of {total}",
                                anchor = Tolerant.Known(TextAnchor.BOTTOM_LEFT),
                                offsetX = 0.08f,
                                sizeSp = 18f,
                            ),
                        ),
                ),
            export =
                TemplateExport(
                    fps = 30,
                    loopSafe = false,
                    segment = TemplateSegment.WholeTrack,
                ),
        )

    private fun master4K(): VideoTemplate =
        starter(
            slug = "master-4k",
            name = "Master 4K",
            notes = "Archive render: 4K, sixty frames, no overlays. The one you keep.",
            look =
                TemplateLook(
                    sceneId = SceneIds.DEFAULT,
                    attack = 0.65f,
                    decay = 0.12f,
                    params =
                        SceneParams(
                            speed = 1.0f,
                            audioDrive = 1.2f,
                            beatResponse = 1.1f,
                            bloom = 0.45f,
                            contrast = 1.05f,
                            paramFadeSec = 0.9f,
                        ),
                ),
            layout =
                TemplateLayout(
                    ratio = Tolerant.Known(ExportRatio.R16_9),
                    artwork = Tolerant.Known(ArtworkStyle.NONE),
                    progress = Tolerant.Known(ProgressStyle.NONE),
                ),
            text = TemplateText(slots = emptyList()),
            export =
                TemplateExport(
                    quality = Tolerant.Known(ExportQuality.UHD4K),
                    fps = 60,
                    loopSafe = false,
                    segment = TemplateSegment.WholeTrack,
                    fileNamePattern = "{artist} - {title} (4K)",
                ),
        )

    private fun starter(
        slug: String,
        name: String,
        notes: String,
        look: TemplateLook,
        layout: TemplateLayout,
        text: TemplateText,
        export: TemplateExport,
    ): VideoTemplate =
        VideoTemplate(
            id = TemplateId.fixed(ID_PREFIX + slug),
            name = name,
            look = look,
            layout = layout,
            text = text,
            export = export,
            author = AUTHOR,
            notes = notes,
            origin = TemplateOrigin.BUNDLED,
        )

    private const val ID_PREFIX = "geode.starter."
    private const val AUTHOR = "Geode"
    private const val HOOK_15S = 15_000L
    private const val HOOK_30S = 30_000L
    private const val HOOK_45S = 45_000L
}

/**
 * The template library. Deliberately narrow: callers hand over text or a stream and
 * get a sealed result back, so nothing above this layer has to know about JSON,
 * file names or Android URIs.
 */
interface TemplateRepository {
    val templates: StateFlow<List<VideoTemplate>>

    /** The bundled starter set, for browsing. Not part of [templates] until adopted. */
    val starters: List<VideoTemplate>

    suspend fun refresh()

    suspend fun list(): List<VideoTemplate>

    suspend fun find(id: TemplateId): VideoTemplate?

    /**
     * Saves a template into the library.
     *
     * Saving one of the bundled starters saves a copy under a fresh id instead: the
     * starter set is read-only, so editing "Reel Neon" can never change what that
     * name means for the next person who reaches for it.
     */
    suspend fun save(template: VideoTemplate): TemplateWrite

    suspend fun delete(id: TemplateId): Boolean

    /** Copies a starter (or any template) into the library under a fresh identity. */
    suspend fun adopt(
        starter: VideoTemplate,
        atMs: Long = System.currentTimeMillis(),
    ): TemplateImport

    /** Imports from pasted text: a whole template file, or a geode://template link. */
    suspend fun importText(text: String): TemplateImport

    /**
     * Imports from a stream the caller opens, so this layer never touches a
     * ContentResolver. Reading stops past [TemplateFormat.MAX_FILE_BYTES].
     */
    suspend fun importFrom(open: () -> InputStream?): TemplateImport

    /** Writes the template to a destination the caller opens, such as a picked file. */
    suspend fun exportTo(
        template: VideoTemplate,
        open: () -> OutputStream?,
    ): TemplateWrite

    /** A nicely named copy in the cache, ready to hand to a share sheet. */
    suspend fun stageForShare(template: VideoTemplate): File?

    /** The whole template as one pasteable link, or null when it is too long. */
    fun shareLink(template: VideoTemplate): String?

    fun fileOf(id: TemplateId): File?
}

class FileTemplateRepository(
    private val store: TemplateStore,
    private val shareDir: File,
) : TemplateRepository {
    private val _templates = MutableStateFlow<List<VideoTemplate>>(emptyList())
    override val templates: StateFlow<List<VideoTemplate>> = _templates.asStateFlow()

    override val starters: List<VideoTemplate> = BundledTemplates.ALL

    override suspend fun refresh() {
        _templates.value = withContext(Dispatchers.IO) { store.list() }
    }

    override suspend fun list(): List<VideoTemplate> = withContext(Dispatchers.IO) { store.list() }

    override suspend fun find(id: TemplateId): VideoTemplate? = withContext(Dispatchers.IO) { store.find(id) }

    override suspend fun save(template: VideoTemplate): TemplateWrite =
        if (BundledTemplates.isBundled(template.id)) {
            writeResultOf(adopt(template))
        } else {
            storeThenRefresh(template.copy(origin = TemplateOrigin.SAVED))
        }

    override suspend fun delete(id: TemplateId): Boolean {
        val deleted = withContext(Dispatchers.IO) { store.delete(id) }
        refresh()
        return deleted
    }

    override suspend fun adopt(
        starter: VideoTemplate,
        atMs: Long,
    ): TemplateImport = persist(starter.asNewCopy(starter.name, atMs))

    override suspend fun importText(text: String): TemplateImport {
        val parse = withContext(Dispatchers.Default) { TemplateFormat.decode(text) }
        return when (parse) {
            is TemplateParse.Parsed -> persist(parse.template)
            is TemplateParse.NotATemplate -> TemplateImport.Unreadable(parse.why)
            is TemplateParse.Malformed -> TemplateImport.Unreadable(parse.why)
        }
    }

    override suspend fun importFrom(open: () -> InputStream?): TemplateImport {
        val text = withContext(Dispatchers.IO) { runCatching { open()?.use(::readBounded) }.getOrNull() }
        return if (text == null) TemplateImport.Unreadable("that file could not be read") else importText(text)
    }

    override suspend fun exportTo(
        template: VideoTemplate,
        open: () -> OutputStream?,
    ): TemplateWrite =
        withContext(Dispatchers.IO) {
            val bytes = TemplateFormat.encode(template).toByteArray(Charsets.UTF_8)
            val written =
                runCatching {
                    val out = open() ?: return@runCatching false
                    out.use { it.write(bytes) }
                    true
                }.getOrDefault(false)
            if (written) TemplateWrite.Written else TemplateWrite.Failed("could not write the template file")
        }

    override suspend fun stageForShare(template: VideoTemplate): File? =
        withContext(Dispatchers.IO) {
            val file = File(shareDir, TemplateFormat.fileNameFor(template))
            if (AtomicWrite.text(file, TemplateFormat.encode(template))) file else null
        }

    override fun shareLink(template: VideoTemplate): String? = TemplateFormat.toLink(template)

    override fun fileOf(id: TemplateId): File? = store.fileOf(id)

    /**
     * Stores an incoming template, matching on id so a re-import updates rather than
     * duplicates. Only a genuinely new template gets its name made unique; replacing
     * keeps whatever the author called it.
     */
    private suspend fun persist(incoming: VideoTemplate): TemplateImport {
        val outcome =
            withContext(Dispatchers.IO) {
                val existing = store.list()
                val previous = existing.firstOrNull { it.id == incoming.id }
                val name = if (previous == null) uniqueName(incoming.name, existing.map { it.name }) else incoming.name
                val stored = incoming.copy(name = name, origin = TemplateOrigin.SAVED)
                when (val write = store.save(stored)) {
                    TemplateWrite.Written ->
                        if (previous == null) {
                            TemplateImport.Added(stored)
                        } else {
                            TemplateImport.Replaced(stored, previous.name)
                        }
                    is TemplateWrite.Failed -> TemplateImport.WriteFailed(write.why)
                }
            }
        refresh()
        return outcome
    }

    private suspend fun storeThenRefresh(template: VideoTemplate): TemplateWrite {
        val result = withContext(Dispatchers.IO) { store.save(template) }
        refresh()
        return result
    }

    private fun writeResultOf(outcome: TemplateImport): TemplateWrite =
        when (outcome) {
            is TemplateImport.Added -> TemplateWrite.Written
            is TemplateImport.Replaced -> TemplateWrite.Written
            is TemplateImport.Unreadable -> TemplateWrite.Failed(outcome.why)
            is TemplateImport.WriteFailed -> TemplateWrite.Failed(outcome.why)
        }

    companion object {
        /**
         * Share copies live in cacheDir: the OS can reclaim them once the chat app has
         * taken what it needs, and they never clutter the user's real library.
         */
        fun from(context: Context): FileTemplateRepository =
            FileTemplateRepository(
                store = TemplateStore(context),
                shareDir = File(context.cacheDir, SHARE_DIR_NAME),
            )

        private const val SHARE_DIR_NAME = "share/templates"
        private const val READ_BUFFER_BYTES = 16 * 1024
        private const val FALLBACK_NAME = "Template"

        private fun uniqueName(
            base: String,
            taken: List<String>,
        ): String {
            val stem = base.trim().ifBlank { FALLBACK_NAME }
            if (taken.none { it.equals(stem, ignoreCase = true) }) return stem
            var attempt = 2
            var candidate = "$stem $attempt"
            while (taken.any { it.equals(candidate, ignoreCase = true) }) {
                attempt++
                candidate = "$stem $attempt"
            }
            return candidate
        }

        /** Reads at most [TemplateFormat.MAX_FILE_BYTES]; anything bigger is not a template. */
        private fun readBounded(input: InputStream): String? {
            val out = ByteArrayOutputStream()
            val buffer = ByteArray(READ_BUFFER_BYTES)
            var read = input.read(buffer)
            while (read >= 0 && out.size() <= TemplateFormat.MAX_FILE_BYTES) {
                out.write(buffer, 0, read)
                read = input.read(buffer)
            }
            return if (out.size() > TemplateFormat.MAX_FILE_BYTES) null else out.toByteArray().toString(Charsets.UTF_8)
        }
    }
}
