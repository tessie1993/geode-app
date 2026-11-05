package dev.geode.render.scene

import dev.geode.engine.bridge.GeodeNative
import java.io.File

/**
 * The config params schema (core/viz/ConfigTable.inc, #423): the default and range of each value the data
 * stores keep beside the scene params, read once from [GeodeNative.configSchemaText] as [ParamSchema] reads
 * its table, so no store keeps a copy of a number the engine also holds. JVM tests read the checked-in
 * core/tests/config_schema.txt through [TEXT_FILE_PROPERTY]; the native config_schema_test proves it equal.
 */
object ConfigSchema {
    const val TEXT_FILE_PROPERTY: String = "geode.configSchema"

    private val groups: Map<String, List<ConfigSpec>> by lazy {
        parse(System.getProperty(TEXT_FILE_PROPERTY)?.let { File(it).readText() } ?: GeodeNative.configSchemaText())
    }

    /** A group's rows in index order (the lfo and adsr wire floats, the macro knobs); empty for an unknown group. */
    fun group(group: String): List<ConfigSpec> = groups[group].orEmpty()

    /** Row [name] of [group]; throws for a row native lacks, which a store would otherwise misread. */
    operator fun get(
        group: String,
        name: String,
    ): ConfigSpec = group(group).first { it.name == name }

    /**
     * Reads a `configs|1|<count>` header (core/viz/Config.hpp's kTextVersion) and its
     * `config|group|index|name|KIND|default|min|max|UNIT` rows, by group. Throws on another version, a
     * malformed row, an index out of its group's order or a wrong count: a misread row moves a value.
     */
    internal fun parse(text: String): Map<String, List<ConfigSpec>> {
        val rows = text.lines().filter { it.isNotBlank() }.map { it.split('|') }
        check(rows.firstOrNull() == listOf("configs", "1", "${rows.size - 1}")) { "config schema header ${rows.firstOrNull()}" }
        val groups = rows.drop(1).onEach { f -> check(f.size == 9 && f[0] == "config") { "config row $f" } }.groupBy { it[1] }
        groups.values.forEach { g -> g.forEachIndexed { i, f -> check(f[2] == "$i") { "config row $f is not index $i of its group" } } }
        return groups.mapValues { (_, g) -> g.map { f -> ConfigSpec(f[3], f[5].toFloat(), f[6].toFloat()..f[7].toFloat()) } }
    }
}

/** One row (core/viz/Config.hpp's `Spec`); ints and flags carry [default] and [range] as floats, like the wire. */
data class ConfigSpec(
    val name: String,
    val default: Float,
    val range: ClosedFloatingPointRange<Float>,
)
