// Geode video templates — the portable "one file in, one finished video out" unit.
//
// A template bundles a look (the scene and its parameters, the same shape a Preset stores) with the layout, text
// and export settings a video is made with. Applying a template today applies its look
// (TemplateController.applyTemplate); the other sections are stored and round-tripped, not rendered.
//
// The file format is native (core/viz/TemplateFile): reading, every default, the clamps, the keys and tokens a
// newer build wrote (carried, never dropped) and writing. Here a template is the text native wrote for it plus the
// identity and look native read from it, which is all the UI shows or applies. The library at the end of this
// file keeps the files (bytes only: every file name, an import's new name and the starter set are native too,
// core/viz/LookFiles).

package dev.geode.data

import android.content.Context
import dev.geode.RingLog
import dev.geode.engine.bridge.LooksNative
import dev.geode.model.PresetLink
import dev.geode.model.SceneParams
import dev.geode.model.StyleCatalogue
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream

/**
 * Stable identity of a template, carried inside the shared file.
 *
 * Re-importing the same file updates the template already in the library instead of
 * piling up copies, which is what makes a template tradeable rather than disposable.
 * Native reads and mints every id; the shell only compares them.
 */
@JvmInline
value class TemplateId internal constructor(
    val value: String,
) {
    override fun toString(): String = value
}

/**
 * Where a template in the library came from.
 *
 * Deliberately *not* written to the file, and deliberately only two states: a
 * template shared over WhatsApp must not claim to be bundled on the receiver's
 * device, and once a file is in the library there is nothing on disk that could
 * honestly distinguish "I made this" from "someone sent me this".
 */
enum class TemplateOrigin {
    /** Shipped with the app; browsable, adopted by copying. */
    BUNDLED,

    /** In this device's library, however it got there. */
    SAVED,
}

/**
 * The whole tradeable unit. One template, one file: [document] is that file's text as native wrote it, and [id],
 * [name] and [look] are what native read from it. A new name or identity goes through [TemplateCodec], which
 * rewrites the text, so the two never disagree. [origin] is local knowledge and is not in the file.
 */
data class VideoTemplate(
    val id: TemplateId,
    val name: String,
    val look: TemplateLook,
    val document: String,
    val origin: TemplateOrigin = TemplateOrigin.SAVED,
)

/** The visual identity: exactly what a [Preset] carries, minus the preset's name. */
data class TemplateLook(
    val sceneId: String,
    val attack: Float,
    val decay: Float,
    val params: SceneParams,
    val customShader: String? = null,
) {
    /** Bridges into the existing preset machinery so applying a template reuses it. */
    fun toPreset(name: String): Preset =
        Preset(
            name = name,
            sceneId = sceneId,
            attack = attack,
            decay = decay,
            customShader = customShader,
            params = params,
        )
}

/**
 * Outcome of reading a template file, link or pasted blob.
 *
 * A failure is a value, never an exception: callers show the reason and leave the
 * original bytes alone rather than deleting or rewriting something they could not
 * understand.
 */
sealed interface TemplateParse {
    data class Parsed(
        val template: VideoTemplate,
    ) : TemplateParse

    /** Valid JSON, but not a Geode template. */
    data class NotATemplate(
        val why: String,
    ) : TemplateParse

    /** Not readable as a template at all. */
    data class Malformed(
        val why: String,
    ) : TemplateParse
}

/**
 * The shell's side of the native template codec (core/viz/TemplateFile): text in, text out. A link's payload is
 * unpacked by [PresetLink], whose bounded-inflate guard serves preset and template links alike.
 */
internal object TemplateCodec {
    private const val PARSED = 1
    private const val NOT_A_TEMPLATE = 0
    private const val STRINGS = 6

    /** A whole template file or a pasted message holding one or a geode://template link. */
    fun read(text: String): TemplateParse {
        val link = LooksNative.templateLinkIn(text)
        return readBody(if (link == null) text else PresetLink.decode(link).orEmpty())
    }

    /** Whether [text] holds a geode://template link. */
    fun hasLink(text: String): Boolean = LooksNative.templateLinkIn(text) != null

    /** A new template of [look] named [name], every other section at the format's defaults. */
    fun create(
        name: String,
        look: TemplateLook,
        atMs: Long,
    ): VideoTemplate =
        written(
            LooksNative.templateCreate(
                name,
                atMs,
                look.sceneId,
                look.attack,
                look.decay,
                look.customShader,
                look.params.frame(),
                look.params.customPaletteId,
            ),
        )

    fun renamed(
        template: VideoTemplate,
        name: String,
    ): VideoTemplate = written(LooksNative.templateRenamed(template.document, name)).copy(origin = template.origin)

    /** A fresh, independent copy: new identity, no inherited authorship, so editing it never overwrites the original. */
    fun newCopy(
        template: VideoTemplate,
        name: String,
        atMs: Long,
    ): VideoTemplate = written(LooksNative.templateNewCopy(template.document, name, atMs))

    /** The whole template as one pasteable link, or null when it is too long for a chat client. */
    fun link(template: VideoTemplate): String? {
        val presetLink = PresetLink.encode(template.document)
        return LooksNative.templateLinkOf(presetLink, PresetLink.MAX_LINK_LENGTH)
    }

    /** The name a shared copy of [template] arrives under. */
    fun shareName(template: VideoTemplate): String = LooksNative.templateShareName(template.name)

    private fun written(text: String?): VideoTemplate =
        checkNotNull((text?.let(::readBody) as? TemplateParse.Parsed)?.template) { "native wrote no template" }

    private fun readBody(body: String): TemplateParse {
        val frame = FloatArray(SceneParams.FIELDS)
        val strings = arrayOfNulls<String>(STRINGS)
        val envelope = FloatArray(2)
        return when (LooksNative.templateRead(body, frame, strings, envelope)) {
            PARSED ->
                // A look of a style this build no longer has is not read, so it is never applied.
                if (strings[3].orEmpty() !in StyleCatalogue.ids) {
                    TemplateParse.NotATemplate("no such style")
                } else {
                    TemplateParse.Parsed(
                        VideoTemplate(
                            id = TemplateId(strings[1].orEmpty()),
                            name = strings[2].orEmpty(),
                            look =
                                TemplateLook(
                                    sceneId = strings[3].orEmpty(),
                                    attack = envelope[0],
                                    decay = envelope[1],
                                    params = SceneParams.read(frame, strings[5].orEmpty()),
                                    customShader = strings[4],
                                ),
                            document = strings[0].orEmpty(),
                        ),
                    )
                }
            NOT_A_TEMPLATE -> TemplateParse.NotATemplate(strings[0].orEmpty())
            else -> TemplateParse.Malformed(strings[0].orEmpty())
        }
    }
}

// ---------------------------------------------------------------------------
// The library
// ---------------------------------------------------------------------------

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
    ) : TemplateImport

    data class Unreadable(
        val why: String,
    ) : TemplateImport

    data class WriteFailed(
        val why: String,
    ) : TemplateImport
}

/**
 * The template library under filesDir/templates, one `<name>--<id>.geode.json` per template, and the list the UI
 * collects. Bytes and files only: a template's text is native's ([TemplateCodec]), and every file name, the name an
 * import takes and the starter set are native (core/viz/LookFiles). A file this build cannot read is skipped, never
 * deleted or rewritten (a newer build's, or a good one this build misreads). An import matches on [TemplateId], so it
 * updates the template already there instead of breeding copies. Every write goes through [AtomicWrite].
 */
class TemplateRepository(
    private val dir: File,
    private val shareDir: File,
) {
    private val _templates = MutableStateFlow<List<VideoTemplate>>(emptyList())
    val templates: StateFlow<List<VideoTemplate>> = _templates.asStateFlow()

    /** The bundled starter set, for browsing; not in [templates] until adopted ([adopt] copies one under a fresh id). */
    val starters: List<VideoTemplate> by lazy {
        List(LooksNative.templateStarterCount()) { LooksNative.templateStarter(it) }
            .mapNotNull { (TemplateCodec.read(it) as? TemplateParse.Parsed)?.template?.copy(origin = TemplateOrigin.BUNDLED) }
    }

    suspend fun refresh() {
        _templates.value = list()
    }

    suspend fun list(): List<VideoTemplate> = withContext(Dispatchers.IO) { readAll() }

    suspend fun find(id: TemplateId): VideoTemplate? = withContext(Dispatchers.IO) { fileOf(id)?.let(::readOrSkip) }

    /** A new template of [look] under [name], not yet saved. */
    fun create(
        name: String,
        look: TemplateLook,
        atMs: Long = System.currentTimeMillis(),
    ): VideoTemplate = TemplateCodec.create(name, look, atMs)

    /** Saving a bundled starter saves a copy under a fresh id instead: the starter set is read-only. */
    suspend fun save(template: VideoTemplate): TemplateWrite {
        if (LooksNative.templateIsStarter(template.id.value)) return writeResultOf(adopt(template))
        val result = withContext(Dispatchers.IO) { write(template) }
        refresh()
        return result
    }

    suspend fun delete(id: TemplateId): Boolean {
        val deleted = withContext(Dispatchers.IO) { fileOf(id)?.delete() ?: false }
        refresh()
        return deleted
    }

    suspend fun adopt(
        starter: VideoTemplate,
        atMs: Long = System.currentTimeMillis(),
    ): TemplateImport = persist(TemplateCodec.newCopy(starter, starter.name, atMs))

    /** Imports from pasted text: a whole template file, or a geode://template link. */
    suspend fun importText(text: String): TemplateImport =
        when (val parse = withContext(Dispatchers.Default) { TemplateCodec.read(text) }) {
            is TemplateParse.Parsed -> persist(parse.template)
            is TemplateParse.NotATemplate -> TemplateImport.Unreadable(parse.why)
            is TemplateParse.Malformed -> TemplateImport.Unreadable(parse.why)
        }

    /** Imports from a stream the caller opens, so this layer never touches a ContentResolver. */
    suspend fun importFrom(open: () -> InputStream?): TemplateImport {
        val text = withContext(Dispatchers.IO) { runCatching { open()?.use(::readBounded) }.getOrNull() }
        return if (text == null) TemplateImport.Unreadable("that file could not be read") else importText(text)
    }

    /**
     * A nicely named copy in the cache, ready to hand to a share sheet. The copies of earlier shares
     * go first: each name is the template's, so they would otherwise pile up one per template shared.
     */
    suspend fun stageForShare(template: VideoTemplate): File? =
        withContext(Dispatchers.IO) {
            shareDir.listFiles().orEmpty().forEach { it.delete() }
            File(shareDir, TemplateCodec.shareName(template)).takeIf { AtomicWrite.text(it, template.document) }
        }

    /** The whole template as one pasteable link, or null when it is too long. */
    fun shareLink(template: VideoTemplate): String? = TemplateCodec.link(template)

    fun fileOf(id: TemplateId): File? {
        val tail = LooksNative.templateFileTail(id.value, FILE_SUFFIX)
        return dir.listFiles().orEmpty().firstOrNull { it.isFile && it.name.endsWith(tail) }
    }

    private suspend fun persist(incoming: VideoTemplate): TemplateImport {
        val outcome =
            withContext(Dispatchers.IO) {
                val existing = readAll()
                val replaces = existing.any { it.id == incoming.id }
                val taken = existing.map { it.name }.toTypedArray()
                val unique = if (replaces) incoming.name else LooksNative.templateUniqueName(incoming.name, taken)
                val stored = if (unique == incoming.name) incoming else TemplateCodec.renamed(incoming, unique)
                when (val write = write(stored)) {
                    TemplateWrite.Written -> if (replaces) TemplateImport.Replaced(stored) else TemplateImport.Added(stored)
                    is TemplateWrite.Failed -> TemplateImport.WriteFailed(write.why)
                }
            }
        refresh()
        return outcome
    }

    private fun write(template: VideoTemplate): TemplateWrite {
        val destination = File(dir, LooksNative.templateFileName(template.name, template.id.value, FILE_SUFFIX))
        val previous = fileOf(template.id)?.takeIf { it.absolutePath != destination.absolutePath }
        if (!AtomicWrite.text(destination, template.document)) {
            return TemplateWrite.Failed("could not save ${destination.name}")
        }
        previous?.delete()
        return TemplateWrite.Written
    }

    private fun readAll(): List<VideoTemplate> {
        val read =
            dir
                .listFiles()
                .orEmpty()
                .filter { it.isFile && it.name.endsWith(FILE_SUFFIX) }
                .mapNotNull(::readOrSkip)
        return LooksNative.looksOrder(read.map { it.name }.toTypedArray(), true).map { read[it] }
    }

    private fun readOrSkip(file: File): VideoTemplate? {
        val text = runCatching { file.readText() }.getOrNull() ?: return null
        val why =
            when (val parse = TemplateCodec.read(text)) {
                is TemplateParse.Parsed -> return parse.template.copy(origin = TemplateOrigin.SAVED)
                is TemplateParse.NotATemplate -> parse.why
                is TemplateParse.Malformed -> parse.why
            }
        RingLog.note(TAG, "template kept but not loaded: ${file.name} ($why)")
        return null
    }

    private fun writeResultOf(outcome: TemplateImport): TemplateWrite =
        when (outcome) {
            is TemplateImport.Added, is TemplateImport.Replaced -> TemplateWrite.Written
            is TemplateImport.Unreadable -> TemplateWrite.Failed(outcome.why)
            is TemplateImport.WriteFailed -> TemplateWrite.Failed(outcome.why)
        }

    companion object {
        private const val TAG = "TemplateStore"
        private const val READ_BUFFER_BYTES = 16 * 1024

        /**
         * Double extension on purpose: `.json` is what messengers and file pickers know how to attach and preview,
         * while `.geode` in front keeps templates recognisable in a crowded Downloads folder.
         */
        private const val FILE_SUFFIX: String = ".geode.json"

        /** A template is kilobytes; anything past this is not one, so readers stop early. */
        private const val MAX_FILE_BYTES: Int = 4 * 1024 * 1024

        /** Share copies live in cacheDir: the OS can reclaim them once the chat app has what it needs. */
        fun from(context: Context): TemplateRepository =
            TemplateRepository(File(context.filesDir, "templates").apply { mkdirs() }, File(context.cacheDir, "share/templates"))

        /** Reads at most [MAX_FILE_BYTES]; anything bigger is not a template. */
        private fun readBounded(input: InputStream): String? {
            val out = ByteArrayOutputStream()
            val buffer = ByteArray(READ_BUFFER_BYTES)
            var read = input.read(buffer)
            while (read >= 0 && out.size() <= MAX_FILE_BYTES) {
                out.write(buffer, 0, read)
                read = input.read(buffer)
            }
            return if (out.size() > MAX_FILE_BYTES) null else out.toByteArray().toString(Charsets.UTF_8)
        }
    }
}
