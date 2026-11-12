package dev.geode.render.scene

import dev.geode.engine.bridge.GeodeNative
import java.io.File

/**
 * The engine's scene and style catalogue, as the shell sees it.
 *
 * Every fact here is native (core/viz/Catalogue, ADR 0003): which style ids exist and in what order the picker lists them,
 * which renderer ([SceneKind]) builds each one, the Styles group each is shown in, the style families, which Customize
 * controls reach which styles ([ParamScope]), and the ranges a few controls quote. The shell asks once, through
 * [GeodeNative.catalogueText], and parses the line table it gets back; nothing in Kotlin keeps a second copy that could
 * drift. What stays Kotlin is not engine: display labels ([VisualStyleCatalog.label]) and `pro/FreeTier`.
 *
 * JVM unit tests cannot load libgeode. The build sets the [TEXT_FILE_PROPERTY] system property on
 * every unit-test task to `core/tests/catalogue.txt`, a checked-in copy of the table that the native
 * `catalogue_test` proves byte-equal to what C++ emits, so the tests read the same catalogue the
 * device does. On a device the property is never set.
 */
object Catalogue {
    /** The table version this shell reads; core/viz/Catalogue.hpp's `kTextVersion`. */
    const val VERSION: Int = 3

    const val TEXT_FILE_PROPERTY: String = "geode.catalogue"

    private val table: Table by lazy {
        Table.parse(System.getProperty(TEXT_FILE_PROPERTY)?.let { File(it).readText() } ?: GeodeNative.catalogueText())
    }

    /** Every style, in picker order: [picker]'s families one after another. */
    val styles: List<CatalogueStyle> get() = table.styles

    /** The Styles page's families, one flat level, by name, each with its styles in list order; every style is in one. */
    val picker: Map<String, List<String>> get() = table.picker

    /** The style families, in picker order. */
    val families: List<StyleFamily> get() = table.families

    fun style(id: String): CatalogueStyle? = table.byId[id]

    /** [id], or the style that replaced it when it is retired (`catalogue::resolveId`): Water opens on Pool. */
    fun resolve(id: String): String = table.retired[id] ?: id

    /**
     * The renderer [sceneId] builds. Unknown ids are [SceneKind.SILK]: the default style is a Silk
     * one and the native registry falls back to it, so the Customize panel describes what will
     * actually be on screen rather than an id nothing can render.
     */
    fun kindOf(sceneId: String): SceneKind = style(sceneId)?.kind ?: SceneKind.SILK

    fun reaches(
        scope: ParamScope,
        sceneId: String,
    ): Boolean = scope in table.kindScopes[kindOf(sceneId)].orEmpty() || style(sceneId)?.scopes?.contains(scope) == true

    /** The scope of the control named [paramKey] (a [ParamKeys] string); unlisted keys are [ParamScope.UNIVERSAL]. */
    fun scopeOf(paramKey: String): ParamScope = table.keyScopes[paramKey] ?: ParamScope.UNIVERSAL

    /** The range the control named [paramKey] offers. Throws for a key the catalogue has no range for. */
    fun range(paramKey: String): ClosedFloatingPointRange<Float> = table.ranges.getValue(paramKey)

    /** The value style [sceneId] holds the control [paramKey] at, or null when the user's own value stands. */
    fun override(
        sceneId: String,
        paramKey: String,
    ): Float? = table.overrides[sceneId to paramKey]

    /** A named engine number (`fluidTiers`, `touchRotationPerDegree`). */
    fun value(name: String): Float = table.values.getValue(name)

    private class Table(
        val styles: List<CatalogueStyle>,
        val families: List<StyleFamily>,
        val picker: Map<String, List<String>>,
        val kindScopes: Map<SceneKind, Set<ParamScope>>,
        val keyScopes: Map<String, ParamScope>,
        val ranges: Map<String, ClosedFloatingPointRange<Float>>,
        val values: Map<String, Float>,
        val overrides: Map<Pair<String, String>, Float>,
        val retired: Map<String, String>,
    ) {
        val byId: Map<String, CatalogueStyle> = styles.associateBy { it.id }

        companion object {
            fun parse(text: String): Table {
                val styles = mutableListOf<CatalogueStyle>()
                val families = mutableListOf<StyleFamily>()
                val picker = linkedMapOf<String, List<String>>()
                val kindScopes = mutableMapOf<SceneKind, Set<ParamScope>>()
                val keyScopes = mutableMapOf<String, ParamScope>()
                val ranges = mutableMapOf<String, ClosedFloatingPointRange<Float>>()
                val values = mutableMapOf<String, Float>()
                val overrides = mutableMapOf<Pair<String, String>, Float>()
                val retired = mutableMapOf<String, String>()
                val rows = text.lines().filter { it.isNotBlank() }.map { it.split('|') }
                check(rows.firstOrNull() == listOf("catalogue", VERSION.toString())) {
                    "native catalogue is ${rows.firstOrNull()}, this shell reads version $VERSION"
                }
                for (f in rows.drop(1)) {
                    when (f[0]) {
                        "kind" -> kindScopes[SceneKind.valueOf(f[1])] = scopes(f.getOrNull(2))
                        "retired" -> retired[f[1]] = f[2]
                        "style" -> styles += CatalogueStyle(f[1], SceneKind.valueOf(f[2]), f[3], scopes(f.getOrNull(4)))
                        "family" -> families += StyleFamily(name = f[1], prefix = f[2], library = f[3], ids = f[4].split(','))
                        "picker" -> picker[f[1]] = f[2].split(',')
                        "override" -> overrides[f[1] to f[2]] = f[3].toFloat()
                        "key" -> keyScopes[f[1]] = ParamScope.valueOf(f[2])
                        "range" -> ranges[f[1]] = f[2].toFloat()..f[3].toFloat()
                        "value" -> values[f[1]] = f[2].toFloat()
                    }
                }
                return Table(styles, families, picker, kindScopes, keyScopes, ranges, values, overrides, retired)
            }

            private fun scopes(field: String?): Set<ParamScope> =
                field
                    ?.split(',')
                    ?.filter { it.isNotEmpty() }
                    ?.mapTo(mutableSetOf()) { ParamScope.valueOf(it) }
                    .orEmpty()
        }
    }
}

/** One style id, its renderer, its Styles group by name, and its per-style scopes. */
data class CatalogueStyle(
    val id: String,
    val kind: SceneKind,
    val look: String,
    val scopes: Set<ParamScope>,
)
