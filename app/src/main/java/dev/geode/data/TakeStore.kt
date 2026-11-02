package dev.geode.data

import android.content.Context
import androidx.annotation.WorkerThread
import dev.geode.editor.ClipContent
import dev.geode.editor.IdSource
import dev.geode.editor.LaneKind
import dev.geode.editor.ProjectId
import dev.geode.editor.StudioProject
import dev.geode.editor.TakeSpan
import dev.geode.editor.TrackRef
import java.util.UUID

/**
 * A take as Studio lists it: its project, since every take lives in one.
 *
 * [name] is the project's name, which is also how the take is addressed by [TakeStore]'s callers;
 * [durationMs] the song time it covers; [eventCount] how many look changes it holds; [trackUri] the
 * song on its audio lane, null for a take recorded without a song.
 */
data class TakeInfo(
    val name: String,
    val durationMs: Long,
    val eventCount: Int,
    val trackUri: String?,
    val sizeBytes: Long,
    val projectId: ProjectId,
    val createdAtMs: Long,
)

/**
 * Takes, stored as projects.
 *
 * Saving a take creates its project folder ([StudioProject.fromTake]): the song on an audio lane and
 * the take as a visual clip on track time, the take's events in `takes/<takeId>.json`, and the
 * export settings copied from Settings › Export's [ExportDestination.CUSTOM] defaults. Listing,
 * loading, renaming and deleting a take act on that project, so a take and its project can never
 * drift apart. A take's name is its project's name and is kept unique among projects.
 *
 * The take files that sat loose in `files/takes` before wave 1 are deleted by [EditorProjectStore]
 * (start fresh, ADR 0005).
 */
class TakeStore(
    private val projects: EditorProjectStore,
) {
    constructor(context: Context) : this(EditorProjectStore(context))

    private val ids = IdSource { UUID.randomUUID().toString() }

    /** Every project that holds a take, newest first. */
    @WorkerThread
    fun list(): List<TakeInfo> =
        projects
            .list()
            .filter { it.takeIds.isNotEmpty() }
            .sortedByDescending { it.createdAtMs }
            .map { p ->
                TakeInfo(
                    name = p.name,
                    durationMs = p.lengthMs,
                    eventCount = p.takeIds.sumOf { takeEvents(p.id, it) },
                    trackUri = p.songUri(),
                    sizeBytes = projects.sizeOf(p.id),
                    projectId = p.id,
                    createdAtMs = p.createdAtMs,
                )
            }

    /** The first take of the project called [name]. */
    @WorkerThread
    fun load(name: String): PerformanceTake.Timeline? {
        val project = byName(name) ?: return null
        val takeId = project.takeIds.firstOrNull() ?: return null
        return runCatching { projects.loadTake(project.id, takeId)?.let(PerformanceTake::Timeline) }
            .onFailure { dev.geode.RingLog.note("TakeStore", "take failed to load: $name", it) }
            .getOrNull()
    }

    @WorkerThread
    fun projectOf(name: String): ProjectId? = byName(name)?.id

    /**
     * Saves the take [json] (from [PerformanceTake.Recorder.finish]) as a new project named [name],
     * or "[name] 2", "[name] 3"… when that name is taken. Returns the name used.
     */
    @WorkerThread
    fun save(
        name: String,
        json: String,
    ): String {
        val taken = projects.list().map { it.name }.toSet()
        var candidate = name
        var n = 2
        while (candidate in taken) {
            candidate = "$name $n"
            n++
        }
        val take = PerformanceTake.Timeline(json)
        val takeId = UUID.randomUUID().toString()
        val project =
            StudioProject.fromTake(
                id = projects.newId(),
                name = candidate,
                take = TakeSpan(takeId, take.firstSceneId, take.trackOffsetMs, take.durationMs),
                track = take.trackUri?.let { TrackRef(uri = it) },
                destination = ExportDestination.CUSTOM,
                defaults = projects.defaultsFor(ExportDestination.CUSTOM),
                nowMs = projects.clock(),
                ids = ids,
            )
        val body =
            if (candidate == name) {
                json
            } else {
                runCatching { org.json.JSONObject(json).put("name", candidate).toString() }.getOrDefault(json)
            }
        projects.saveTake(project.id, takeId, body)
        projects.save(project)
        return candidate
    }

    /** Deletes the take's whole project: a take lives in its project and nowhere else. */
    @WorkerThread
    fun delete(name: String) {
        byName(name)?.let { projects.delete(it.id) }
    }

    @WorkerThread
    fun rename(
        from: String,
        to: String,
    ): Boolean {
        val target = to.trim()
        val project = byName(from) ?: return false
        if (target.isEmpty() || byName(target) != null) return false
        return projects.save(projects.touch(project.copy(name = target)))
    }

    private fun takeEvents(
        id: ProjectId,
        takeId: String,
    ): Int = projects.loadTake(id, takeId)?.let { runCatching { PerformanceTake.Timeline(it).eventCount }.getOrNull() } ?: 0

    private fun byName(name: String): StudioProject? = projects.list().firstOrNull { it.name == name && it.takeIds.isNotEmpty() }

    private fun StudioProject.songUri(): String? =
        edit.timeline.lanes
            .filter { it.kind == LaneKind.Audio }
            .flatMap { it.clips }
            .firstNotNullOfOrNull { (it.content as? ClipContent.Audio)?.uri }
}
