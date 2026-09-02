package dev.geode.data

import dev.geode.editor.BezierCurve
import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.ClipId
import dev.geode.editor.ClipTransition
import dev.geode.editor.EaseShape
import dev.geode.editor.EditorProject
import dev.geode.editor.Interpolation
import dev.geode.editor.Keyframe
import dev.geode.editor.KeyframeId
import dev.geode.editor.KeyframeSheet
import dev.geode.editor.KeyframeTrack
import dev.geode.editor.Lane
import dev.geode.editor.LaneId
import dev.geode.editor.LaneKind
import dev.geode.editor.Marker
import dev.geode.editor.MarkerColour
import dev.geode.editor.MarkerId
import dev.geode.editor.MarkerOrigin
import dev.geode.editor.MarkerSet
import dev.geode.editor.OverlayBlend
import dev.geode.editor.ParamId
import dev.geode.editor.ParamValue
import dev.geode.editor.Timeline
import dev.geode.export.ClipEdit
import dev.geode.export.ClipLook
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio
import org.json.JSONArray
import org.json.JSONObject

/** The on-disk shape of an [EditorProject]. Variants carry a "type"/"kind" tag; unknown tags fail the read. */
internal object EditorProjectJson {
    const val VERSION = 1

    fun toJson(project: EditorProject): String =
        JSONObject()
            .put("version", VERSION)
            .put("timeline", timeline(project.timeline))
            .put("markers", JSONArray(project.markers.markers.map(::marker)))
            .put("keyframes", JSONArray(project.keyframes.tracks.map(::track)))
            .toString(2)

    fun fromJson(json: String): EditorProject {
        val o = JSONObject(json)
        require(o.optInt("version", VERSION) <= VERSION) { "editor project from a newer app" }
        return EditorProject(
            timeline = timeline(o.getJSONObject("timeline")),
            markers = MarkerSet().addAll(o.optJSONArray("markers").items(::marker)),
            keyframes = KeyframeSheet().withTracks(o.optJSONArray("keyframes").items(::track)),
        )
    }

    private fun timeline(t: Timeline): JSONObject =
        JSONObject()
            .put("durationMs", t.durationMs)
            .put("lanes", JSONArray(t.lanes.map(::lane)))

    private fun timeline(o: JSONObject): Timeline =
        Timeline(lanes = o.optJSONArray("lanes").items(::lane), durationMs = o.optLong("durationMs", 0L))

    private fun lane(l: Lane): JSONObject =
        JSONObject()
            .put("id", l.id.value)
            .put("kind", laneKindName(l.kind))
            .put("name", l.name)
            .put("muted", l.muted)
            .put("locked", l.locked)
            .put("clips", JSONArray(l.clips.map(::clip)))

    private fun lane(o: JSONObject): Lane =
        Lane(
            id = LaneId(o.getString("id")),
            kind = laneKind(o.getString("kind")),
            name = o.optString("name", ""),
            muted = o.optBoolean("muted", false),
            locked = o.optBoolean("locked", false),
        ).withClips(o.optJSONArray("clips").items(::clip))

    private fun clip(c: Clip): JSONObject =
        JSONObject()
            .put("id", c.id.value)
            .put("startMs", c.startMs)
            .put("durationMs", c.durationMs)
            .put("sourceInMs", c.sourceInMs)
            .put("sourceDurationMs", c.sourceDurationMs)
            .put("label", c.label)
            .put("enabled", c.enabled)
            .putOpt("transition", c.transition?.let(::transition))
            .put("content", content(c.content))

    private fun clip(o: JSONObject): Clip =
        Clip(
            id = ClipId(o.getString("id")),
            content = content(o.getJSONObject("content")),
            startMs = o.getLong("startMs"),
            durationMs = o.getLong("durationMs"),
            sourceInMs = o.optLong("sourceInMs", 0L),
            sourceDurationMs = o.optLong("sourceDurationMs", 0L),
            label = o.optString("label", ""),
            enabled = o.optBoolean("enabled", true),
            transition = o.optJSONObject("transition")?.let(::transition),
        )

    private fun transition(t: ClipTransition): JSONObject = JSONObject().put("id", t.id).put("durationMs", t.durationMs)

    private fun transition(o: JSONObject): ClipTransition =
        ClipTransition(o.getString("id"), o.optLong("durationMs", ClipTransition.DEFAULT_TRANSITION_MS))

    private fun content(c: ClipContent): JSONObject =
        when (c) {
            is ClipContent.Scene ->
                JSONObject()
                    .put("type", "scene")
                    .put("sceneId", c.sceneId)
                    .putOpt("presetId", c.presetId)
                    .putOpt("milkPath", c.milkPath)
            is ClipContent.Video -> JSONObject().put("type", "video").put("uri", c.uri).put("edit", clipEdit(c.edit))
            is ClipContent.Still -> JSONObject().put("type", "still").put("uri", c.uri).put("kenBurns", c.kenBurns.toDouble())
            is ClipContent.Text -> JSONObject().put("type", "text").put("text", c.text).putOpt("styleId", c.styleId)
            is ClipContent.Overlay ->
                JSONObject()
                    .put("type", "overlay")
                    .put("uri", c.uri)
                    .put("blend", c.blend.name)
                    .put("opacity", c.opacity.toDouble())
            is ClipContent.Audio -> JSONObject().put("type", "audio").put("uri", c.uri).put("gainDb", c.gainDb.toDouble())
        }

    private fun content(o: JSONObject): ClipContent =
        when (val type = o.getString("type")) {
            "scene" -> ClipContent.Scene(o.getString("sceneId"), o.stringOrNull("presetId"), o.stringOrNull("milkPath"))
            "video" -> ClipContent.Video(o.getString("uri"), o.optJSONObject("edit")?.let(::clipEdit) ?: ClipEdit())
            "still" -> ClipContent.Still(o.getString("uri"), o.optDouble("kenBurns", 0.0).toFloat())
            "text" -> ClipContent.Text(o.getString("text"), o.stringOrNull("styleId"))
            "overlay" ->
                ClipContent.Overlay(
                    o.getString("uri"),
                    enumOr(o.optString("blend"), OverlayBlend.SCREEN),
                    o.optDouble("opacity", 1.0).toFloat(),
                )
            "audio" -> ClipContent.Audio(o.getString("uri"), o.optDouble("gainDb", 0.0).toFloat())
            else -> throw IllegalArgumentException("unknown clip content: $type")
        }

    private fun clipEdit(e: ClipEdit): JSONObject =
        JSONObject()
            .put("startMs", e.startMs)
            .put("endMs", e.endMs)
            .put("look", e.look.name)
            .put("brightness", e.brightness.toDouble())
            .put("contrast", e.contrast.toDouble())
            .put("saturation", e.saturation.toDouble())
            .put("hueDegrees", e.hueDegrees.toDouble())
            .put("monochrome", e.monochrome)
            .put("invert", e.invert)
            .put("speed", e.speed.toDouble())
            .put("rotationDegrees", e.rotationDegrees.toDouble())
            .putOpt("ratio", e.ratio?.name)
            .put("quality", e.quality.name)
            .put("mute", e.mute)
            .put("caption", e.caption)
            .putOpt("lutUri", e.lutUri)
            .put("gammaRed", e.gammaRed.toDouble())
            .put("gammaGreen", e.gammaGreen.toDouble())
            .put("gammaBlue", e.gammaBlue.toDouble())

    private fun clipEdit(o: JSONObject): ClipEdit =
        ClipEdit(
            startMs = o.optLong("startMs", 0L),
            endMs = o.optLong("endMs", 0L),
            look = enumOr(o.optString("look"), ClipLook.NONE),
            brightness = o.optDouble("brightness", 0.0).toFloat(),
            contrast = o.optDouble("contrast", 0.0).toFloat(),
            saturation = o.optDouble("saturation", 0.0).toFloat(),
            hueDegrees = o.optDouble("hueDegrees", 0.0).toFloat(),
            monochrome = o.optBoolean("monochrome", false),
            invert = o.optBoolean("invert", false),
            speed = o.optDouble("speed", 1.0).toFloat(),
            rotationDegrees = o.optDouble("rotationDegrees", 0.0).toFloat(),
            ratio = o.stringOrNull("ratio")?.let { name -> ExportRatio.entries.firstOrNull { it.name == name } },
            quality = enumOr(o.optString("quality"), ExportQuality.FHD1080),
            mute = o.optBoolean("mute", false),
            caption = o.optString("caption", ""),
            lutUri = o.stringOrNull("lutUri"),
            gammaRed = o.optDouble("gammaRed", 1.0).toFloat(),
            gammaGreen = o.optDouble("gammaGreen", 1.0).toFloat(),
            gammaBlue = o.optDouble("gammaBlue", 1.0).toFloat(),
        )

    private fun marker(m: Marker): JSONObject =
        JSONObject()
            .put("id", m.id.value)
            .put("atMs", m.atMs)
            .put("name", m.name)
            .put("colour", m.colour.name)
            .put("note", m.note)
            .put(
                "origin",
                when (val origin = m.origin) {
                    MarkerOrigin.Manual -> JSONObject().put("type", "manual")
                    is MarkerOrigin.TappedIn ->
                        JSONObject().put("type", "tapped").put("rawAtMs", origin.rawAtMs).put("latencyMs", origin.latencyCompensationMs)
                    is MarkerOrigin.Detected ->
                        JSONObject()
                            .put("type", "detected")
                            .put("confidence", origin.confidence.toDouble())
                            .put("strength", origin.strength.toDouble())
                },
            )

    private fun marker(o: JSONObject): Marker {
        val origin = o.optJSONObject("origin")
        return Marker(
            id = MarkerId(o.getString("id")),
            atMs = o.getLong("atMs"),
            name = o.optString("name", ""),
            colour = enumOr(o.optString("colour"), MarkerColour.CYAN),
            note = o.optString("note", ""),
            origin =
                when (val type = origin?.optString("type", "manual") ?: "manual") {
                    "manual" -> MarkerOrigin.Manual
                    "tapped" -> MarkerOrigin.TappedIn(origin!!.getLong("rawAtMs"), origin.getLong("latencyMs"))
                    "detected" ->
                        MarkerOrigin.Detected(
                            origin!!.optDouble("confidence", 0.0).toFloat(),
                            origin.optDouble("strength", 0.0).toFloat(),
                        )
                    else -> throw IllegalArgumentException("unknown marker origin: $type")
                },
        )
    }

    private fun track(t: KeyframeTrack): JSONObject =
        JSONObject()
            .put("paramId", t.paramId.value)
            .putOpt("clipId", t.clipId?.value)
            .put("enabled", t.enabled)
            .put("keys", JSONArray(t.keys.map(::key)))

    private fun track(o: JSONObject): KeyframeTrack =
        KeyframeTrack(
            paramId = ParamId(o.getString("paramId")),
            clipId = o.stringOrNull("clipId")?.let(::ClipId),
            enabled = o.optBoolean("enabled", true),
            keys = o.optJSONArray("keys").items(::key).sortedBy { it.atMs },
        )

    private fun key(k: Keyframe): JSONObject =
        JSONObject()
            .put("id", k.id.value)
            .put("atMs", k.atMs)
            .put("value", value(k.value))
            .put("interpolation", interpolation(k.interpolation))

    private fun key(o: JSONObject): Keyframe =
        Keyframe(
            id = KeyframeId(o.getString("id")),
            atMs = o.getLong("atMs"),
            value = value(o.getJSONObject("value")),
            interpolation = o.optJSONObject("interpolation")?.let(::interpolation) ?: Interpolation.Linear,
        )

    private fun value(v: ParamValue): JSONObject =
        when (v) {
            is ParamValue.Scalar -> JSONObject().put("kind", "scalar").put("value", v.value.toDouble())
            is ParamValue.Vector2 -> JSONObject().put("kind", "vector2").put("x", v.x.toDouble()).put("y", v.y.toDouble())
            is ParamValue.Colour ->
                JSONObject()
                    .put("kind", "colour")
                    .put("r", v.r.toDouble())
                    .put("g", v.g.toDouble())
                    .put("b", v.b.toDouble())
                    .put("a", v.a.toDouble())
            is ParamValue.Toggle -> JSONObject().put("kind", "toggle").put("on", v.on)
            is ParamValue.Choice -> JSONObject().put("kind", "choice").put("index", v.index)
        }

    private fun value(o: JSONObject): ParamValue =
        when (val kind = o.getString("kind")) {
            "scalar" -> ParamValue.Scalar(o.getDouble("value").toFloat())
            "vector2" -> ParamValue.Vector2(o.getDouble("x").toFloat(), o.getDouble("y").toFloat())
            "colour" ->
                ParamValue.Colour(
                    o.getDouble("r").toFloat(),
                    o.getDouble("g").toFloat(),
                    o.getDouble("b").toFloat(),
                    o.optDouble("a", 1.0).toFloat(),
                )
            "toggle" -> ParamValue.Toggle(o.getBoolean("on"))
            "choice" -> ParamValue.Choice(o.getInt("index"))
            else -> throw IllegalArgumentException("unknown keyframe value: $kind")
        }

    private fun interpolation(i: Interpolation): JSONObject =
        when (i) {
            Interpolation.Hold -> JSONObject().put("type", "hold")
            Interpolation.Linear -> JSONObject().put("type", "linear")
            is Interpolation.Ease -> JSONObject().put("type", "ease").put("shape", i.shape.name)
            is Interpolation.Custom ->
                JSONObject()
                    .put("type", "custom")
                    .put("c1x", i.curve.c1x.toDouble())
                    .put("c1y", i.curve.c1y.toDouble())
                    .put("c2x", i.curve.c2x.toDouble())
                    .put("c2y", i.curve.c2y.toDouble())
        }

    private fun interpolation(o: JSONObject): Interpolation =
        when (val type = o.getString("type")) {
            "hold" -> Interpolation.Hold
            "linear" -> Interpolation.Linear
            "ease" -> Interpolation.Ease(enumOr(o.optString("shape"), EaseShape.IN_OUT))
            "custom" ->
                Interpolation.Custom(
                    BezierCurve(
                        o.getDouble("c1x").toFloat(),
                        o.getDouble("c1y").toFloat(),
                        o.getDouble("c2x").toFloat(),
                        o.getDouble("c2y").toFloat(),
                    ),
                )
            else -> throw IllegalArgumentException("unknown interpolation: $type")
        }

    private fun laneKindName(kind: LaneKind): String =
        when (kind) {
            LaneKind.Visual -> "visual"
            LaneKind.Media -> "media"
            LaneKind.Text -> "text"
            LaneKind.Overlay -> "overlay"
            LaneKind.Audio -> "audio"
        }

    private fun laneKind(name: String): LaneKind =
        when (name) {
            "visual" -> LaneKind.Visual
            "media" -> LaneKind.Media
            "text" -> LaneKind.Text
            "overlay" -> LaneKind.Overlay
            "audio" -> LaneKind.Audio
            else -> throw IllegalArgumentException("unknown lane kind: $name")
        }

    private inline fun <reified T : Enum<T>> enumOr(
        name: String,
        fallback: T,
    ): T = enumValues<T>().firstOrNull { it.name == name } ?: fallback

    private fun JSONObject.stringOrNull(key: String): String? = if (has(key) && !isNull(key)) getString(key) else null

    private fun <T> JSONArray?.items(read: (JSONObject) -> T): List<T> =
        if (this == null) emptyList() else List(length()) { read(getJSONObject(it)) }
}
