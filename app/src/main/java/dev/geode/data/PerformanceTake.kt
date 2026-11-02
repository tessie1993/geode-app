package dev.geode.data

import dev.geode.render.scene.SceneParams
import org.json.JSONArray
import org.json.JSONObject

/**
 * A take: a live performance recorded as timed look changes, every style switch, Milk preset and
 * param change included, so replaying or rendering it shows exactly what was performed.
 *
 * Times are **track time**: the song position each change was made at, not the wall clock. That is
 * what makes a take survive pause and seek and land on the beat it was played on when it becomes a
 * visual clip over its song (`editor/StudioProject.fromTake`):
 * - while the song is paused, track time stands still, so nothing is timed into the pause; a change
 *   made while paused is written when the song moves on (or by [Recorder.finish]);
 * - a seek forward leaves a gap the take simply holds its last look across;
 * - a seek back is a punch-in: everything recorded from that point on is dropped and recorded again,
 *   the way an overdub works.
 * A take recorded with no song (mic or another app's audio) passes the wall clock in as its time.
 *
 * On disk the event times are relative to the take's start, `trackOffsetMs`, the earliest song
 * position recorded. That keeps the file format the export path already reads.
 */
object PerformanceTake {
    const val MIN_KEYFRAME_GAP_MS = 80L

    const val MAX_EVENTS = 45_000

    data class State(
        val sceneId: String,
        val params: SceneParams,
        val milkPath: String?,
    )

    /**
     * Records a take. [startTrackMs] is the song position when recording began; every [append] is
     * timed by the song position at the moment of the change.
     */
    class Recorder(
        sceneId: String,
        params: SceneParams,
        milkPath: String?,
        startTrackMs: Long = 0L,
        private val maxEvents: Int = MAX_EVENTS,
    ) {
        /** Events with absolute track times, in time order. Each holds only what changed since the one before. */
        private val events = ArrayList<JSONObject>()
        private var accum = PresetStore.paramsToJson(params)
        private var lastSceneId = sceneId
        private var lastMilkPath = milkPath
        private var lastAtMs = startTrackMs
        private var endTrackMs = startTrackMs

        val size: Int get() = events.size

        val hasRoom: Boolean get() = events.size < maxEvents

        /** The earliest song position recorded: where the take begins. */
        val startTrackMs: Long get() = events.firstOrNull()?.optLong("t", 0L) ?: 0L

        /** The style the take opens on. */
        val firstSceneId: String get() = events.firstOrNull()?.optString("s", "").orEmpty()

        /** Song time covered: from [startTrackMs] to the furthest position the song reached while recording. */
        val durationMs: Long get() = (endTrackMs - startTrackMs).coerceAtLeast(0L)

        init {
            events += snapshot(startTrackMs, sceneId, params, milkPath)
        }

        /** Record the look at [atTrackMs]. False when nothing was written (too soon, unchanged, or full). */
        fun append(
            atTrackMs: Long,
            sceneId: String,
            params: SceneParams,
            milkPath: String?,
        ): Boolean {
            if (atTrackMs < lastAtMs - MIN_KEYFRAME_GAP_MS) return punchIn(atTrackMs, sceneId, params, milkPath)
            endTrackMs = maxOf(endTrackMs, atTrackMs)
            if (!hasRoom) return false
            if (atTrackMs - lastAtMs < MIN_KEYFRAME_GAP_MS) return false
            return write(atTrackMs, sceneId, params, milkPath)
        }

        /**
         * The take's JSON. [atTrackMs] and the look are the state at stop: written even when it is
         * too soon after the last change, so a style chosen just before stopping (or while paused) is
         * kept.
         */
        fun finish(
            name: String,
            trackUri: String?,
            atTrackMs: Long,
            sceneId: String,
            params: SceneParams,
            milkPath: String?,
        ): String {
            if (atTrackMs < lastAtMs - MIN_KEYFRAME_GAP_MS) {
                punchIn(atTrackMs, sceneId, params, milkPath)
            } else if (hasRoom) {
                write(maxOf(atTrackMs, lastAtMs), sceneId, params, milkPath)
            }
            endTrackMs = maxOf(endTrackMs, atTrackMs, lastAtMs)
            val origin = startTrackMs
            val relative = JSONArray()
            events.forEach { relative.put(JSONObject(it.toString()).put("t", it.optLong("t", origin) - origin)) }
            return JSONObject()
                .put("name", name)
                .put("durationMs", durationMs)
                .put("trackOffsetMs", origin)
                .apply { if (trackUri != null) put("trackUri", trackUri) }
                .put("events", relative)
                .toString()
        }

        private fun write(
            atMs: Long,
            sceneId: String,
            params: SceneParams,
            milkPath: String?,
        ): Boolean {
            val next = PresetStore.paramsToJson(params)
            val delta = diff(accum, next)
            val sceneChanged = sceneId != lastSceneId
            val milkChanged = milkPath != lastMilkPath
            if (delta.length() == 0 && !sceneChanged && !milkChanged) return false
            val event = JSONObject().put("t", atMs)
            if (sceneChanged) event.put("s", sceneId)
            if (milkChanged && milkPath != null) event.put("m", milkPath)
            if (delta.length() > 0) event.put("p", delta)
            events += event
            accum = next
            lastSceneId = sceneId
            lastMilkPath = milkPath
            lastAtMs = atMs
            return true
        }

        /**
         * A seek back to [atMs]: drop what was recorded from there on and start again from the look
         * on screen now, as a full snapshot (the deltas before it no longer add up to it).
         */
        private fun punchIn(
            atMs: Long,
            sceneId: String,
            params: SceneParams,
            milkPath: String?,
        ): Boolean {
            events.removeAll { it.optLong("t", 0L) >= atMs }
            events += snapshot(atMs, sceneId, params, milkPath)
            accum = PresetStore.paramsToJson(params)
            lastSceneId = sceneId
            lastMilkPath = milkPath
            lastAtMs = atMs
            endTrackMs = atMs
            return true
        }

        private fun snapshot(
            atMs: Long,
            sceneId: String,
            params: SceneParams,
            milkPath: String?,
        ): JSONObject =
            JSONObject()
                .put("t", atMs)
                .put("s", sceneId)
                .put("p", PresetStore.paramsToJson(params))
                .apply { if (milkPath != null) put("m", milkPath) }
    }

    /** One look switch in a take: at take time [atMs], style [sceneId] with preset [milkPath]. */
    data class StyleChange(
        val atMs: Long,
        val sceneId: String,
        val milkPath: String?,
    )

    /** A recorded take read back; [stateAt] takes take time (ms from the take's start). */
    class Timeline(
        json: String,
    ) {
        private val root = JSONObject(json)
        private val events: JSONArray = root.optJSONArray("events") ?: JSONArray()

        val name: String = root.optString("name", "Take")
        val trackUri: String? = root.optString("trackUri", "").takeIf { it.isNotEmpty() }
        val durationMs: Long = root.optLong("durationMs", 0L)

        /** The song position the take starts at. */
        val trackOffsetMs: Long = root.optLong("trackOffsetMs", 0L)
        val eventCount: Int get() = events.length()

        val isEmpty: Boolean get() = events.length() == 0

        /** The style the take opens on, or "" for an empty take. */
        val firstSceneId: String get() = events.optJSONObject(0)?.optString("s", "").orEmpty()

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

        /**
         * Every switch of style or MilkDrop preset in the take, in order, each with the full look it
         * switches to (take time, style, preset). An export hands them to the native renderer, which
         * times them (`core/viz/StyleSchedule`), so a take renders every style it was performed in.
         */
        fun styleChanges(): List<StyleChange> {
            val out = mutableListOf<StyleChange>()
            var scene = ""
            var milk: String? = null
            for (i in 0 until events.length()) {
                val e = events.optJSONObject(i)
                val s = e?.optString("s", "")?.takeIf { it.isNotEmpty() }
                val m = e?.optString("m", "")?.takeIf { it.isNotEmpty() }
                if (e != null && (s != null || m != null)) {
                    s?.let { scene = it }
                    m?.let { milk = it }
                    if (scene.isNotEmpty()) out += StyleChange(e.optLong("t", 0L), scene, milk)
                }
            }
            return out
        }

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
