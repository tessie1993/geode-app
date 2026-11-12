package dev.geode.data

import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectRender
import dev.geode.editor.ProjectTemplate
import dev.geode.editor.StudioProject
import dev.geode.engine.bridge.GeodeNative
import dev.geode.export.ExportCodec
import org.json.JSONArray
import org.json.JSONObject

/**
 * The on-disk shapes of the Studio model: a [StudioProject] (a project folder's `project.json`: the
 * edit plus kind, export settings and renders) and a [ProjectTemplate] (a template folder's
 * `template.json`). The kind and the edit are native's documents (`core/studio`): native reads them,
 * with their defaults, order and unknown tags, and this file embeds the JSON it hands back as it is.
 * A part native refuses fails the read, and a file from a newer app is refused rather than half-read.
 */
internal object EditorProjectJson {
    /** Version of `project.json` and `template.json`; the edit carries its own. */
    const val FOLDER_VERSION = 1

    fun projectToJson(p: StudioProject): String =
        JSONObject()
            .put("version", FOLDER_VERSION)
            .put("id", p.id.value)
            .put("name", p.name)
            .put("kind", JSONObject(p.kind))
            .put("destination", p.destination.name)
            .put("export", export(p.export))
            .put("createdAtMs", p.createdAtMs)
            .put("updatedAtMs", p.updatedAtMs)
            .putOpt("templateId", p.templateId?.value)
            .put("renders", JSONArray(p.renders.map(::render)))
            .put("edit", JSONObject(p.edit))
            .toString(2)

    fun projectFromJson(json: String): StudioProject {
        val o = JSONObject(json)
        require(o.optInt("version", FOLDER_VERSION) <= FOLDER_VERSION) { "Studio project from a newer app" }
        return StudioProject(
            id = ProjectId(o.getString("id")),
            name = o.optString("name", ""),
            kind = kind(o),
            edit = edit(o),
            destination = ExportDestination.byName(o.stringOrNull("destination")),
            export = o.optJSONObject("export")?.let(::export) ?: ExportDefaults(),
            renders = o.optJSONArray("renders").items(::render),
            createdAtMs = o.optLong("createdAtMs", 0L),
            updatedAtMs = o.optLong("updatedAtMs", 0L),
            templateId = o.stringOrNull("templateId")?.let(::ProjectId),
        )
    }

    fun templateFromJson(json: String): ProjectTemplate {
        val o = JSONObject(json)
        require(o.optInt("version", FOLDER_VERSION) <= FOLDER_VERSION) { "Studio template from a newer app" }
        return ProjectTemplate(
            id = ProjectId(o.getString("id")),
            name = o.optString("name", ""),
            kind = kind(o),
            edit = edit(o),
            destination = ExportDestination.byName(o.stringOrNull("destination")),
            export = o.optJSONObject("export")?.let(::export) ?: ExportDefaults(),
            lengthMs = o.optLong("lengthMs", 0L),
            createdAtMs = o.optLong("createdAtMs", 0L),
        )
    }

    private fun kind(o: JSONObject): String =
        requireNotNull(GeodeNative.studioKindRead(o.optJSONObject("kind")?.toString())) { "unknown Studio project kind" }

    private fun edit(o: JSONObject): String =
        requireNotNull(GeodeNative.studioEditRead(o.optJSONObject("edit")?.toString())) { "unreadable Studio edit" }

    private fun export(d: ExportDefaults): JSONObject =
        JSONObject()
            .put("quality", d.quality.name)
            .put("fps", d.fps)
            .put("ratio", d.ratio.name)
            .put("loopSafe", d.loopSafe)
            .put("codec", d.codec.name)
            .put("loudnessTargetId", d.loudnessTargetId)
            .put("saveTo", d.saveTo.encode())

    /** Unknown or missing values fall back one by one, so a project never fails to open over its export settings. */
    private fun export(o: JSONObject): ExportDefaults {
        val d = ExportDefaults()
        return ExportDefaults(
            quality = enumOr(o.optString("quality"), d.quality),
            fps = o.optInt("fps", d.fps).let { if (it in EXPORT_FPS_OPTIONS) it else d.fps },
            ratio = enumOr(o.optString("ratio"), d.ratio),
            loopSafe = o.optBoolean("loopSafe", d.loopSafe),
            codec = enumOr(o.optString("codec"), ExportCodec.H264),
            loudnessTargetId = o.stringOrNull("loudnessTargetId") ?: d.loudnessTargetId,
            saveTo = SaveTarget.decode(o.stringOrNull("saveTo")),
        )
    }

    private fun render(r: ProjectRender): JSONObject =
        JSONObject()
            .put("uri", r.uri)
            .put("label", r.label)
            .put("createdAtMs", r.createdAtMs)
            .put("durationMs", r.durationMs)

    private fun render(o: JSONObject): ProjectRender =
        ProjectRender(
            uri = o.getString("uri"),
            label = o.optString("label", ""),
            createdAtMs = o.optLong("createdAtMs", 0L),
            durationMs = o.optLong("durationMs", 0L),
        )

    private inline fun <reified T : Enum<T>> enumOr(
        name: String,
        fallback: T,
    ): T = enumValues<T>().firstOrNull { it.name == name } ?: fallback

    private fun JSONObject.stringOrNull(key: String): String? = if (has(key) && !isNull(key)) getString(key) else null

    private fun <T> JSONArray?.items(read: (JSONObject) -> T): List<T> =
        if (this == null) emptyList() else List(length()) { read(getJSONObject(it)) }
}
