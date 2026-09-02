package dev.geode.render

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

/**
 * Catalog of scene-to-scene transitions, both built-in and loaded from the GL Transitions asset.
 *
 * Public because the UI layer lists transitions for the user to pick; the shader programs
 * they compile to stay internal to this module.
 */
object TransitionCatalog {
    private const val ASSET = "gl_transitions.json"

    data class Param(
        val name: String,
        val type: String,
        val values: FloatArray,
    ) {
        override fun equals(other: Any?): Boolean =
            other is Param && name == other.name && type == other.type && values.contentEquals(other.values)

        override fun hashCode(): Int = (name.hashCode() * 31 + type.hashCode()) * 31 + values.contentHashCode()
    }

    data class Def(
        val name: String,
        val author: String,
        val license: String,
        val params: List<Param>,
        val glsl: String,
    )

    private val BUILT_IN_BY_ID: Map<String, TransitionStyle> =
        TransitionStyle.entries.associateBy { it.name.lowercase() }

    val BUILT_IN_IDS: List<String> = TransitionStyle.entries.map { it.name.lowercase() }

    @Volatile
    private var library: List<Def>? = null

    @Volatile
    private var libraryByName: Map<String, Def>? = null

    fun library(context: Context): List<Def> {
        library?.let { return it }
        val parsed =
            runCatching {
                val text =
                    context.assets
                        .open(ASSET)
                        .bufferedReader()
                        .use { it.readText() }
                val arr = JSONArray(text)
                (0 until arr.length()).map { i -> parseDef(arr.getJSONObject(i)) }
            }.getOrDefault(emptyList())
        libraryByName = parsed.associateBy { it.name }
        library = parsed
        return parsed
    }

    fun allIds(context: Context): List<String> = BUILT_IN_IDS + library(context).map { it.name }

    fun definition(
        context: Context,
        id: String,
    ): Def? {
        if (id in BUILT_IN_BY_ID) return null
        library(context)
        return libraryByName?.get(id)
    }

    fun builtIn(id: String): TransitionStyle? = BUILT_IN_BY_ID[id]

    private fun parseDef(o: JSONObject): Def {
        val types = o.optJSONObject("paramsTypes")
        val defaults = o.optJSONObject("defaultParams")
        val params =
            types
                ?.keys()
                ?.asSequence()
                ?.map { key ->
                    Param(key, types.optString(key, "float"), floatsOf(defaults?.opt(key)))
                }?.toList()
                .orEmpty()
        return Def(
            name = o.optString("name"),
            author = o.optString("author"),
            license = o.optString("license"),
            params = params,
            glsl = o.optString("glsl"),
        )
    }

    private fun floatsOf(value: Any?): FloatArray =
        when (value) {
            null -> floatArrayOf(0f)
            is Boolean -> floatArrayOf(if (value) 1f else 0f)
            is Number -> floatArrayOf(value.toFloat())
            is JSONArray -> FloatArray(value.length()) { i -> value.optDouble(i, 0.0).toFloat() }
            else -> floatArrayOf(0f)
        }
}
