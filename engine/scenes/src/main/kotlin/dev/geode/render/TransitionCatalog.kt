package dev.geode.render

import android.content.Context
import dev.geode.engine.bridge.GeodeNative

/**
 * Catalog of scene-to-scene transitions, both built-in and loaded from the GL Transitions asset.
 *
 * Public because the UI layer lists transitions for the user to pick; the shader programs
 * they compile to stay internal to this module.
 */
object TransitionCatalog {
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
        // Parsed natively (core/viz/TransitionLibrary.hpp), the same parse the renderer's composite uses.
        val table = runCatching { String(GeodeNative.transitionsTable(context.assets), Charsets.UTF_8) }.getOrDefault("")
        val parsed = parseTable(table)
        libraryByName = parsed.associateBy { it.name }
        library = parsed
        return parsed
    }

    fun definition(
        context: Context,
        id: String,
    ): Def? {
        if (id in BUILT_IN_BY_ID) return null
        library(context)
        return libraryByName?.get(id)
    }

    fun builtIn(id: String): TransitionStyle? = BUILT_IN_BY_ID[id]

    /** The table of `encodeTransitionTable`: defs by RS, fields by US, params by GS, param fields by FS. */
    internal fun parseTable(table: String): List<Def> =
        if (table.isEmpty()) {
            emptyList()
        } else {
            table.split('\u001e').map { def ->
                val f = def.split('\u001f', limit = 5)
                val params =
                    f[3].split('\u001d').filter { it.isNotEmpty() }.map {
                        val p = it.split('\u001c')
                        Param(p[0], p[1], p[2].split(',').mapNotNull(String::toFloatOrNull).toFloatArray())
                    }
                Def(name = f[0], author = f[1], license = f[2], params = params, glsl = f[4])
            }
        }
}
