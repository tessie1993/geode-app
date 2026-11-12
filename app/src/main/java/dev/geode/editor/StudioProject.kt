package dev.geode.editor

import dev.geode.data.ExportDefaults
import dev.geode.data.ExportDestination

/*
 * The Studio project model (wave 1, issue #231): what a project is and what a template is.
 *
 * A project is a folder. It holds its edit (timeline, markers, keyframes), its own export settings
 * and the list of every video rendered from it. Studio opens to the list of these folders; there is
 * no global "Untitled" any more. Storage is `data/EditorProjectStore`; nothing here touches a file.
 *
 * The kind and the edit are native's documents (`core/studio`): the shell keeps the canonical JSON
 * native hands back as it is and asks native for every rule on them, so nothing here reads one.
 */

@JvmInline
value class ProjectId(
    val value: String,
)

/**
 * One video rendered from a project. The file itself lives where the user saved it (the Videos
 * library or a chosen folder), so the project keeps a reference, not the bytes: [uri] is the
 * MediaStore or document URI the render wrote.
 */
data class ProjectRender(
    val uri: String,
    val label: String,
    val createdAtMs: Long,
    val durationMs: Long,
)

/** The song a project is built over. [durationMs] 0 means "not known yet". */
data class TrackRef(
    val uri: String,
    val title: String = "",
    val durationMs: Long = 0L,
)

/**
 * A Studio project: one folder on disk.
 *
 * [kind] (standard, or a loop drawn once and repeated to a length) and [edit] are native's JSON
 * (`GeodeNative.studioKindRead`, `studioEditRead`, `studioEditNew`).
 *
 * [export] is the project's own copy of the export settings. It starts as [destination]'s defaults
 * from Settings › Export and then belongs to the project: changing it here never touches Settings,
 * and changing Settings never touches an existing project.
 *
 * [templateId] remembers which template the project was made from, for display only; the project
 * holds its own copy of everything the template gave it.
 */
data class StudioProject(
    val id: ProjectId,
    val name: String,
    val kind: String,
    val edit: String,
    val destination: ExportDestination = ExportDestination.CUSTOM,
    val export: ExportDefaults = ExportDefaults(),
    val renders: List<ProjectRender> = emptyList(),
    val createdAtMs: Long = 0L,
    val updatedAtMs: Long = 0L,
    val templateId: ProjectId? = null,
) {
    /** Newest first, which is how My videos lists them. */
    fun withRender(render: ProjectRender): StudioProject = copy(renders = (listOf(render) + renders.filterNot { it.uri == render.uri }))

    fun withoutRender(uri: String): StudioProject = copy(renders = renders.filterNot { it.uri == uri })
}

/**
 * A template: a project minus its audio (no audio lanes, markers or renders), as a template
 * folder's `template.json` holds it. [lengthMs] is the length of the song it was made over.
 */
data class ProjectTemplate(
    val id: ProjectId,
    val name: String,
    val kind: String,
    val edit: String,
    val destination: ExportDestination = ExportDestination.CUSTOM,
    val export: ExportDefaults = ExportDefaults(),
    val lengthMs: Long = 0L,
    val createdAtMs: Long = 0L,
)
