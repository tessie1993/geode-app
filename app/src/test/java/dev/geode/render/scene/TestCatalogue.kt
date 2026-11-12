package dev.geode.render.scene

import dev.geode.engine.bridge.GeodeNative

/**
 * A small catalogue for JVM tests, which cannot load the native library: a few real styles of each
 * kind, two families with their openers, the retired id and the one held control. A test that reaches
 * [StyleCatalogue] calls [install] first.
 *
 * It answers in the shape native does (a list is its items each followed by a newline), so the shell's
 * reading is what runs. What the real catalogue holds is `core/tests/catalogue_test.cpp`'s to prove;
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
            "milkdrop" to listOf(GeodeNative.STYLE_MILKDROP),
            "fluid" to listOf("fluid", "fluid_ink", "curlflow"),
            "cymatics" to listOf("cymatics", "kundt_tube"),
        )
    private val all: List<String> = picker.values.flatten()
    private val shader: List<String> = listOf("shaders", "arbor", "terrain").flatMap(picker::getValue)
    private val fluid: List<String> = listOf("fluid", "fluid_ink")
    private val cymatics: List<String> = picker.getValue("cymatics")

    fun install() = StyleCatalogue.install(this)

    override fun styleIds(trait: Int): String =
        when (trait) {
            GeodeNative.STYLE_ANY -> all
            GeodeNative.STYLE_SHADER -> shader
            GeodeNative.STYLE_FAMILY_FOLLOWER -> picker.getValue("arbor") + picker.getValue("terrain") - OPENERS.toSet()
            GeodeNative.STYLE_AUTO_DRAW -> picker.getValue("silk") + shader
            GeodeNative.STYLE_FLUID -> fluid
            GeodeNative.STYLE_CYMATICS -> cymatics
            else -> emptyList()
        }.let(::listText)

    override fun pickerNames(): String = listText(picker.keys)

    override fun pickerIds(family: String): String = listText(picker[family].orEmpty())

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

    override fun held(
        styleId: String,
        paramKey: String,
    ): Float = if (styleId == "kundt_tube" && paramKey == "Geometry") 1f else Float.NaN

    private fun listText(items: Collection<String>): String = items.joinToString("") { "$it\n" }
}
