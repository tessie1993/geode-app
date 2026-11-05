package dev.geode.data

import android.content.Context
import androidx.annotation.WorkerThread
import dev.geode.RingLog
import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectTemplate
import dev.geode.editor.StudioProject
import dev.geode.engine.bridge.GeodeNative
import java.io.File
import java.util.UUID

/**
 * Studio's projects and templates on disk. A project is a folder, and its takes sit in a sibling
 * folder of the same id:
 *
 * ```
 * files/projects/<id>/
 *   project.json        the StudioProject: edit, kind, export settings, renders (EditorProjectJson)
 * files/project-takes/<id>/
 *   <takeId>.json       every take recorded into it (PerformanceTake's format)
 * files/project-templates/<id>/
 *   template.json       a ProjectTemplate: a project minus its audio
 * ```
 *
 * Why the takes are not inside the project folder (#384): a long take is megabytes of JSON, and
 * Android's cloud backup stops backing up *everything* (presets, playlists, history) once the app
 * passes its ~25 MB quota. The backup rules can only exclude a fixed path, never
 * `projects/<any id>/takes`, so the takes live under one root that `backup_rules.xml` and
 * `data_extraction_rules.xml` exclude, while the small `project.json` still backs up and My videos
 * keeps its list after a restore. A phone-to-phone transfer and the manual backup zip carry takes.
 * Takes saved before the move are moved once, when the store is built, by native code
 * (`core/library/StudioFiles`, `GeodeNative.studioMoveTakesOut`).
 *
 * The folder name is the project id, never the user's name for it, so renaming is one field and two
 * projects may share a name. Rendered videos are not in the folder: they live where the user saved
 * them, and `project.json` lists their URIs.
 *
 * Every write is atomic ([AtomicWrite]); a `project.json` that cannot be read is quarantined and
 * left out of [list] rather than failing the whole list. All of it is plain `java.io.File`, so the
 * store runs the same off-device; only the [Context] constructor knows where `filesDir` is.
 *
 * [defaultsFor] is Settings › Export's per-destination defaults, which [StudioProject.create] copies
 * into each new project; it lives here so whoever creates a project needs only this store.
 */
class EditorProjectStore(
    private val projectsDir: File,
    private val templatesDir: File,
    private val takesDir: File = File(projectsDir.parentFile, TAKES_DIR),
    val defaultsFor: (ExportDestination) -> ExportDefaults = { it.factoryDefaults() },
    val clock: () -> Long = System::currentTimeMillis,
) {
    constructor(context: Context) : this(
        projectsDir = File(context.filesDir, PROJECTS_DIR),
        templatesDir = File(context.filesDir, TEMPLATES_DIR),
        takesDir = File(context.filesDir, TAKES_DIR),
        defaultsFor = ExportPrefsStore(GeodePrefsFiles(context).general)::loadFor,
    ) {
        startFresh(context.filesDir)
        // Takes saved inside project folders before #384 move out, once; the move is native.
        val notMoved = GeodeNative.studioMoveTakesOut(projectsDir.path, takesDir.path)
        if (notMoved > 0) RingLog.note(TAG, "$notMoved takes not moved out of their projects")
    }

    fun newId(): ProjectId = ProjectId(UUID.randomUUID().toString())

    fun folderOf(id: ProjectId): File = File(projectsDir, safeId(id))

    /** Every readable project, most recently changed first: the order Studio lists them in. */
    @WorkerThread
    fun list(): List<StudioProject> =
        projectsDir
            .listFiles { f -> f.isDirectory }
            .orEmpty()
            .mapNotNull { read(File(it, PROJECT_FILE), EditorProjectJson::projectFromJson) }
            .sortedByDescending { it.updatedAtMs }

    @WorkerThread
    fun load(id: ProjectId): StudioProject? = read(File(folderOf(id), PROJECT_FILE), EditorProjectJson::projectFromJson)

    /** Writes [project] as it is; callers stamp [StudioProject.updatedAtMs] (see [touch]). */
    @WorkerThread
    fun save(project: StudioProject): Boolean {
        val folder = folderOf(project.id).apply { mkdirs() }
        return AtomicWrite.text(File(folder, PROJECT_FILE), EditorProjectJson.projectToJson(project))
    }

    fun touch(project: StudioProject): StudioProject = project.copy(updatedAtMs = clock())

    /** The folder [id]'s takes are saved in (beside the project folder, not inside it; see the class KDoc). */
    fun takesFolderOf(id: ProjectId): File = File(takesDir, safeId(id))

    /** Removes the folder, its takes and everything in them. The rendered videos stay where they were saved. */
    @WorkerThread
    fun delete(id: ProjectId): Boolean {
        val takesGone = takesFolderOf(id).deleteRecursively()
        return folderOf(id).deleteRecursively() && takesGone
    }

    @WorkerThread
    fun saveTake(
        id: ProjectId,
        takeId: String,
        json: String,
    ): Boolean {
        val dir = takesFolderOf(id).apply { mkdirs() }
        return AtomicWrite.text(File(dir, PresetStore.safeFileName(takeId) + ".json"), json)
    }

    /** The take's JSON (PerformanceTake's format), or null when the project has no such take. */
    @WorkerThread
    fun loadTake(
        id: ProjectId,
        takeId: String,
    ): String? =
        File(takesFolderOf(id), PresetStore.safeFileName(takeId) + ".json")
            .takeIf { it.isFile }
            ?.let { runCatching { it.readText() }.getOrNull() }

    /** Bytes the project takes on disk (project file plus takes), for My videos' details line. */
    @WorkerThread
    fun sizeOf(id: ProjectId): Long =
        listOf(folderOf(id), takesFolderOf(id))
            .flatMap { it.walkTopDown().filter(File::isFile).toList() }
            .sumOf { it.length() }

    @WorkerThread
    fun templates(): List<ProjectTemplate> =
        templatesDir
            .listFiles { f -> f.isDirectory }
            .orEmpty()
            .mapNotNull { read(File(it, TEMPLATE_FILE), EditorProjectJson::templateFromJson) }
            .sortedBy { it.name.lowercase() }

    @WorkerThread
    fun loadTemplate(id: ProjectId): ProjectTemplate? =
        read(File(File(templatesDir, safeId(id)), TEMPLATE_FILE), EditorProjectJson::templateFromJson)

    @WorkerThread
    fun saveTemplate(template: ProjectTemplate): Boolean {
        val folder = File(templatesDir, safeId(template.id)).apply { mkdirs() }
        return AtomicWrite.text(File(folder, TEMPLATE_FILE), EditorProjectJson.templateToJson(template))
    }

    @WorkerThread
    fun deleteTemplate(id: ProjectId): Boolean = File(templatesDir, safeId(id)).deleteRecursively()

    private fun <T> read(
        file: File,
        parse: (String) -> T,
    ): T? {
        if (!file.isFile) return null
        return runCatching { parse(file.readText()) }
            .onFailure {
                RingLog.note(TAG, "unreadable Studio file quarantined: ${file.parentFile?.name}/${file.name}", it)
                AtomicWrite.quarantine(file)
            }.getOrNull()
    }

    /** Ids are UUIDs in practice; anything else is made safe rather than trusted as a path. */
    private fun safeId(id: ProjectId): String = PresetStore.safeFileName(id.value)

    companion object {
        private const val TAG = "EditorProjectStore"
        const val PROJECTS_DIR = "projects"
        const val TEMPLATES_DIR = "project-templates"
        const val PROJECT_FILE = "project.json"
        const val TEMPLATE_FILE = "template.json"

        /** Every project's takes, one folder per project id. Kept out of cloud backup by name (#384). */
        const val TAKES_DIR = "project-takes"

        /**
         * Wave 1 starts fresh (pre-launch, testers only): the one global "Untitled" project in
         * `files/editor` and the loose takes in `files/takes` are deleted, not migrated. A take
         * there was look automation on wall-clock time with no project to live in; see ADR 0005.
         */
        private val LEGACY_DIRS = listOf("editor", "takes")

        private fun startFresh(filesDir: File) {
            LEGACY_DIRS.map { File(filesDir, it) }.filter { it.exists() }.forEach { it.deleteRecursively() }
        }
    }
}
