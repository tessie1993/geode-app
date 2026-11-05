package dev.geode.render.scene

/** The `|`-split rows under a native table's `<header>|<count>` line; throws on another header or count. */
internal fun schemaRows(
    text: String,
    header: String,
): List<List<String>> {
    val rows = text.lines().filter { it.isNotBlank() }.map { it.split('|') }
    check(rows.firstOrNull() == "$header|${rows.size - 1}".split('|')) { "native schema is ${rows.firstOrNull()}, shell reads $header" }
    return rows.drop(1)
}

/** [rows] as `<row>|index|name|KIND|default|min|max|FADE` specs, indexes in order (a last field but FADE snaps). */
internal fun schemaSpecs(
    rows: List<List<String>>,
    row: String,
): List<ParamSpec> =
    rows.mapIndexed { i, f ->
        check(f.size == 8 && f[0] == row && f[1] == "$i") { "$row row $i is $f" }
        ParamSpec(i, f[2], ParamSpec.Kind.valueOf(f[3]), f[4].toFloat(), f[5].toFloat()..f[6].toFloat(), f[7] == "FADE")
    }
