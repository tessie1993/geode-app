package dev.geode.model

import dev.geode.data.NativeLookDefaults
import dev.geode.engine.bridge.CatalogueNative

/**
 * A small catalogue for JVM tests, which cannot load the native library: a few real styles of each
 * kind, two families with their openers, the retired id and the one held control. A test that reaches
 * [StyleCatalogue] calls [install] first.
 *
 * What the real catalogue holds is `core/tests/catalogue_test.cpp`'s to prove;
 * this only gives the shell's own rules (the free tier, the saved formats) something to work on.
 */
object TestCatalogue : StyleCatalogue.Reads {
    /** The style that opens each family here; the rest of a family follows it. */
    val OPENERS: List<String> = listOf("arbor_grove", "terra_ridge")

    private val picker: Map<String, List<String>> =
        linkedMapOf(
            "silk" to listOf("silk_web", "silk_bloom"),
            "shaders" to
                listOf(
                    "julia",
                    "plasma",
                    "bars",
                    "ring",
                    "waves",
                    "aurora",
                    "kifs",
                    "pool",
                    "glow_vortex",
                    "neon_chrome_sea",
                    "black_hole",
                    "hex_hive",
                    "mandelbox_cathedral",
                ),
            "arbor" to listOf("arbor_grove", "arbor_willow", "arbor_coral"),
            "terrain" to listOf("terra_ridge", "terra_dunes"),
            "milkdrop" to listOf(CatalogueNative.STYLE_MILKDROP),
            "fluid" to listOf("fluid", "fluid_ink", "curlflow"),
            "cymatics" to listOf("cymatics", "kundt_tube"),
        )
    private val all: List<String> = picker.values.flatten()
    private val shader: List<String> = listOf("shaders", "arbor", "terrain").flatMap(picker::getValue)
    private val fluid: List<String> = listOf("fluid", "fluid_ink")
    private val cymatics: List<String> = picker.getValue("cymatics")

    /**
     * Installs this catalogue, and [TestParams] with it: the types a catalogue test builds default to [SceneParams.DEFAULT].
     * The look defaults are a stand-in in native's shape; native's own are core/tests/look_snapshot_test's.
     */
    fun install() {
        StyleCatalogue.install(this)
        TestParams.install()
        NativeLookDefaults.install { "neon_chrome_sea\nfade\n1200\n0.6\n0.12\n20\n30\n8\n" }
    }

    override fun styleIds(trait: Int): List<String> =
        when (trait) {
            CatalogueNative.STYLE_ANY -> all
            CatalogueNative.STYLE_SHADER -> shader
            CatalogueNative.STYLE_FAMILY_FOLLOWER -> picker.getValue("arbor") + picker.getValue("terrain") - OPENERS.toSet()
            CatalogueNative.STYLE_AUTO_DRAW -> picker.getValue("silk") + shader
            CatalogueNative.STYLE_FLUID -> fluid
            CatalogueNative.STYLE_CYMATICS -> cymatics
            else -> emptyList()
        }

    override fun styleHas(
        trait: Int,
        styleId: String,
    ): Boolean = styleId in styleIds(trait)

    override fun pickerNames(): List<String> = picker.keys.toList()

    override fun pickerIds(family: String): List<String> = picker[family].orEmpty()

    override fun resolve(styleId: String): String = if (styleId == "water") "pool" else styleId

    override fun presetStyle(styleId: String): String =
        when (styleId) {
            in fluid -> fluid.first()
            in cymatics -> cymatics.first()
            else -> styleId
        }

    /** No control scopes here: every control reaches every style. */
    override fun scopeOf(paramKey: String): String = ParamScope.UNIVERSAL.name

    override fun reaches(
        scope: String,
        styleId: String,
    ): Boolean = true

    override fun holds(
        styleId: String,
        paramKey: String,
    ): Boolean = styleId == "kundt_tube" && paramKey == "Geometry"

    override fun held(
        styleId: String,
        paramKey: String,
    ): Float = if (holds(styleId, paramKey)) 1f else Float.NaN
}
