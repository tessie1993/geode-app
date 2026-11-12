package dev.geode.data

import android.content.Context
import androidx.annotation.WorkerThread
import dev.geode.RingLog
import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectTemplate
import dev.geode.editor.StudioProject
import dev.geode.geodeContainer
import java.io.File
import java.util.UUID

/**
 * Studio's projects and templates on disk. A project is a folder:
 *
 * ```
 * files/projects/<id>/
 *   project.json        the StudioProject: edit, kind, export settings, renders (EditorProjectJson)
 * files/project-templates/<id>/
 *   template.json       a ProjectTemplate: a project minus its audio
 * ```
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
    val defaultsFor: (ExportDestination) -> ExportDefaults = { it.factoryDefaults() },
    val clock: () -> Long = System::currentTimeMillis,
) {
    constructor(context: Context) : this(
        projectsDir = File(context.filesDir, PROJECTS_DIR),
        templatesDir = File(context.filesDir, TEMPLATES_DIR),
        defaultsFor = ExportPrefsStore(context.geodeContainer.prefsFiles.general)::loadFor,
    ) {
        startFresh(context.filesDir)
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

    /** Removes the folder and everything in it. The rendered videos stay where they were saved. */
    @WorkerThread
    fun delete(id: ProjectId): Boolean = folderOf(id).deleteRecursively()

    @WorkerThread
    fun templates(): List<ProjectTemplate> =
        templatesDir
            .listFiles { f -> f.isDirectory }
            .orEmpty()
            .mapNotNull { read(File(it, TEMPLATE_FILE), EditorProjectJson::templateFromJson) }
            .sortedBy { it.name.lowercase() }

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

        /** Where recorded takes were kept before Record became a live video; still out of cloud backup. */
        const val TAKES_DIR = "project-takes"

        /**
         * Deleted, not migrated: the one global "Untitled" project in `files/editor` and the takes
         * (look automation replayed into a render) in `files/takes` and [TAKES_DIR]. Record is a
         * live video now; the owner chose to delete the old takes.
         */
        private val LEGACY_DIRS = listOf("editor", "takes", TAKES_DIR)

        private fun startFresh(filesDir: File) {
            LEGACY_DIRS.map { File(filesDir, it) }.filter { it.exists() }.forEach { it.deleteRecursively() }
        }
    }
}
