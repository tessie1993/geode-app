package dev.geode.data

import dev.geode.render.scene.SceneParams
import org.json.JSONArray
import org.json.JSONObject

object PerformanceTake {
    const val MIN_KEYFRAME_GAP_MS = 80L

    const val MAX_EVENTS = 45_000

    data class State(
        val sceneId: String,
        val params: SceneParams,
        val milkPath: String?,
    )

    class Recorder(
        sceneId: String,
        params: SceneParams,
        milkPath: String?,
        private val maxEvents: Int = MAX_EVENTS,
    ) {
        private val events = JSONArray()
        private var accum = PresetStore.paramsToJson(params)
        private var lastSceneId = sceneId
        private var lastMilkPath = milkPath
        private var lastAtMs = Long.MIN_VALUE

        val size: Int get() = events.length()

        val hasRoom: Boolean get() = events.length() < maxEvents

        init {
            events.put(
                JSONObject()
                    .put("t", 0L)
                    .put("s", sceneId)
                    .put("p", JSONObject(accum.toString()))
                    .apply { if (milkPath != null) put("m", milkPath) },
            )
            lastAtMs = 0L
        }

        fun append(
            atMs: Long,
            sceneId: String,
            params: SceneParams,
            milkPath: String?,
        ): Boolean {
            if (!hasRoom) return false
            if (atMs - lastAtMs < MIN_KEYFRAME_GAP_MS) return false
            val next = PresetStore.paramsToJson(params)
            val delta = diff(accum, next)
            val sceneChanged = sceneId != lastSceneId
            val milkChanged = milkPath != lastMilkPath
            if (delta.length() == 0 && !sceneChanged && !milkChanged) return false
            val event = JSONObject().put("t", atMs)
            if (sceneChanged) event.put("s", sceneId)
            if (milkChanged && milkPath != null) event.put("m", milkPath)
            if (delta.length() > 0) event.put("p", delta)
            events.put(event)
            accum = next
            lastSceneId = sceneId
            lastMilkPath = milkPath
            lastAtMs = atMs
            return true
        }

        fun finish(
            name: String,
            trackUri: String?,
            durationMs: Long,
            trackOffsetMs: Long = 0L,
        ): String =
            JSONObject()
                .put("name", name)
                .put("durationMs", durationMs)
                .put("trackOffsetMs", trackOffsetMs)
                .apply { if (trackUri != null) put("trackUri", trackUri) }
                .put("events", events)
                .toString()
    }

    class Timeline(
        json: String,
    ) {
        private val root = JSONObject(json)
        private val events: JSONArray = root.optJSONArray("events") ?: JSONArray()

        val name: String = root.optString("name", "Take")
        val trackUri: String? = root.optString("trackUri", "").takeIf { it.isNotEmpty() }
        val durationMs: Long = root.optLong("durationMs", 0L)

        val trackOffsetMs: Long = root.optLong("trackOffsetMs", 0L)
        val eventCount: Int get() = events.length()

        val isEmpty: Boolean get() = events.length() == 0

        private var cursor = 0
        private var accum = JSONObject()
        private var sceneId = ""
        private var milkPath: String? = null
        private var appliedThroughMs = Long.MIN_VALUE

        fun stateAt(ms: Long): State? {
            if (events.length() == 0) return null
            if (ms < appliedThroughMs) rewind()
            while (cursor < events.length()) {
                val e = events.optJSONObject(cursor) ?: break
                if (e.optLong("t", 0L) > ms) break
                apply(e)
                cursor++
            }
            appliedThroughMs = ms
            if (cursor == 0) return null
            return State(sceneId, PresetStore.paramsFromJson(accum), milkPath)
        }

        fun lastEventMs(): Long = events.optJSONObject(events.length() - 1)?.optLong("t", 0L) ?: 0L

        private fun rewind() {
            cursor = 0
            accum = JSONObject()
            sceneId = ""
            milkPath = null
            appliedThroughMs = Long.MIN_VALUE
        }

        private fun apply(e: JSONObject) {
            e.optString("s", "").takeIf { it.isNotEmpty() }?.let { sceneId = it }
            e.optString("m", "").takeIf { it.isNotEmpty() }?.let { milkPath = it }
            val p = e.optJSONObject("p") ?: return
            for (key in p.keys()) accum.put(key, p.get(key))
        }
    }

    private fun diff(
        from: JSONObject,
        to: JSONObject,
    ): JSONObject {
        val out = JSONObject()
        for (key in to.keys()) {
            val next = to.get(key)
            if (from.opt(key) != next) out.put(key, next)
        }
        return out
    }
}
