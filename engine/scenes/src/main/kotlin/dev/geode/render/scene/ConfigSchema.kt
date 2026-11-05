package dev.geode.render.scene

import dev.geode.engine.bridge.GeodeNative
import java.io.File

/**
 * The config params schema (core/viz/ConfigTable.inc, #423): the default and range of each value the stores keep
 * beside the scene params, read like [ParamSchema], so no store copies a number the engine holds. JVM tests read
 * core/tests/config_schema.txt ([TEXT_FILE_PROPERTY]), which the native config_schema_test proves equal.
 */
object ConfigSchema {
    const val TEXT_FILE_PROPERTY: String = "geode.configSchema"

    private val groups: Map<String, List<ParamSpec>> by lazy {
        parse(System.getProperty(TEXT_FILE_PROPERTY)?.let { File(it).readText() } ?: GeodeNative.configSchemaText())
    }

    /** A group's rows in index order (the lfo and adsr wire floats, the macro knobs); empty for an unknown group. */
    fun group(group: String): List<ParamSpec> = groups[group].orEmpty()

    /** Row [name] of [group]; throws for a row native lacks, which a store would otherwise misread. */
    operator fun get(
        group: String,
        name: String,
    ): ParamSpec = group(group).first { it.name == name }

    /** A `configs|1|<count>` table (Config.hpp's kTextVersion) of `config|group|index|name|KIND|default|min|max|UNIT` rows, by group. */
    internal fun parse(text: String): Map<String, List<ParamSpec>> =
        schemaRows(text, "configs|1")
            .groupBy({ it.getOrElse(1) { "" } }, { f -> f.filterIndexed { i, _ -> i != 1 } })
            .mapValues { (_, rows) -> schemaSpecs(rows, "config") }
}
