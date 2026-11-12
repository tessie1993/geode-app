package dev.geode.render.scene

import dev.geode.engine.bridge.GeodeNative
import java.util.concurrent.ConcurrentHashMap

/**
 * What the shell knows about styles: the native catalogue's answers (core/viz/Catalogue), asked through
 * `GeodeNative.catalogue*` and kept, because no answer changes while the process lives and the Customize
 * panel asks the same ones on every recomposition.
 *
 * Nothing here decides anything: which styles exist, their order, which family a style is in, what a
 * retired id became and which control reaches which style are all native's. Labels are string resources
 * ([styleLabel]); the Pro rule is `pro/FreeTier`.
 */
object StyleCatalogue {
    /**
     * The style a fresh look, template or project names. A constant, not a question, because it is the
     * default of data classes that are built where no native library is loaded.
     */
    const val DEFAULT_ID: String = "silk_web"

    /** The catalogue's questions. The app asks native; a JVM test, which cannot load the library, installs its own. */
    interface Reads {
        fun styleIds(trait: Int): String

        fun pickerNames(): String

        fun pickerIds(family: String): String

        fun resolve(styleId: String): String

        fun presetStyle(styleId: String): String

        fun scopeOf(paramKey: String): String

        fun reaches(
            scope: String,
            styleId: String,
        ): Boolean

        fun held(
            styleId: String,
            paramKey: String,
        ): Float
    }

    private object Native : Reads {
        override fun styleIds(trait: Int): String = GeodeNative.catalogueStyleIds(trait)

        override fun pickerNames(): String = GeodeNative.cataloguePickerNames()

        override fun pickerIds(family: String): String = GeodeNative.cataloguePickerIds(family)

        override fun resolve(styleId: String): String = GeodeNative.catalogueResolve(styleId)

        override fun presetStyle(styleId: String): String = GeodeNative.cataloguePresetStyle(styleId)

        override fun scopeOf(paramKey: String): String = GeodeNative.catalogueScopeOf(paramKey)

        override fun reaches(
            scope: String,
            styleId: String,
        ): Boolean = GeodeNative.catalogueReaches(scope, styleId)

        override fun held(
            styleId: String,
            paramKey: String,
        ): Float = GeodeNative.catalogueOverride(styleId, paramKey)
    }

    /** One source's answers. Stores and screens ask from more than one thread, hence the concurrent maps. */
    private class Answers(
        val reads: Reads,
    ) {
        val ids = ConcurrentHashMap<Int, List<String>>()
        val sets = ConcurrentHashMap<Int, Set<String>>()
        val shelves = ConcurrentHashMap<String, String>()
        val scopes = ConcurrentHashMap<String, String>()
        val reach = ConcurrentHashMap<String, Boolean>()
        val picker: Map<String, List<String>> by lazy {
            lines(reads.pickerNames()).associateWith { lines(reads.pickerIds(it)) }
        }

        fun ids(trait: Int): List<String> = ids.getOrPut(trait) { lines(reads.styleIds(trait)) }

        fun has(
            trait: Int,
            id: String,
        ): Boolean = id in sets.getOrPut(trait) { ids(trait).toHashSet() }
    }

    @Volatile
    private var answers = Answers(Native)

    /** Asks [reads] from now on and forgets every kept answer. */
    fun install(reads: Reads) {
        answers = Answers(reads)
    }

    /** Every style, in the order the picker lists them and the stage swipes. */
    val ids: List<String> get() = ids(GeodeNative.STYLE_ANY)

    /** The styles with one of `GeodeNative.STYLE_*`'s traits, in the same order. */
    fun ids(trait: Int): List<String> = answers.ids(trait)

    /** The Styles page's families in order, each with its styles. */
    val picker: Map<String, List<String>> get() = answers.picker

    /** True for every look of the Fluid solver, `fluid` itself included; Curl Flow is its own renderer. */
    fun isFluid(id: String): Boolean = answers.has(GeodeNative.STYLE_FLUID, id)

    fun isCymatics(id: String): Boolean = answers.has(GeodeNative.STYLE_CYMATICS, id)

    /** The style a saved id opens on: a retired id's replacement, any other id itself. */
    fun resolve(id: String): String = answers.reads.resolve(id)

    /** The style a saved look of [id] is filed under: every look of Fluid or Cymatics shares one shelf. */
    fun presetStyle(id: String): String = answers.let { a -> a.shelves.getOrPut(id) { a.reads.presetStyle(id) } }

    /** The value [styleId] holds the control [paramKey] at, or null when the control is the user's. */
    fun held(
        styleId: String,
        paramKey: String,
    ): Float? = answers.reads.held(styleId, paramKey).takeUnless { it.isNaN() }

    internal fun scopeOf(paramKey: String): String = answers.let { a -> a.scopes.getOrPut(paramKey) { a.reads.scopeOf(paramKey) } }

    internal fun reaches(
        scope: String,
        styleId: String,
    ): Boolean = answers.let { a -> a.reach.getOrPut("$scope|$styleId") { a.reads.reaches(scope, styleId) } }

    private fun lines(text: String): List<String> = text.split('\n').filter { it.isNotEmpty() }
}

/**
 * Which styles a Customize parameter can actually move, by the native catalogue's name for the scope.
 *
 * WHY (R4, no dead controls): a control that does nothing for the style on screen has to be HIDDEN, not
 * greyed out with an apology under it. Every surface that shows, rolls or modulates a parameter asks
 * [appliesTo], and the answer is native's, so "is this parameter live right now?" has one answer.
 *
 * The constants are the scopes a screen names outright; `catalogue_test` holds native to those names.
 * [UNIVERSAL] is not a shrug: the composite pass applies the parameter to the finished frame for every
 * style, so it really does reach all of them.
 */
@JvmInline
value class ParamScope(
    val name: String,
) {
    fun appliesTo(sceneId: String): Boolean = StyleCatalogue.reaches(name, sceneId)

    companion object {
        val UNIVERSAL = ParamScope("UNIVERSAL")
        val BAND_GAINS = ParamScope("BAND_GAINS")
        val SHADER_LOOK = ParamScope("SHADER_LOOK")
        val MARCH_DETAIL = ParamScope("MARCH_DETAIL")
        val ENDLESS_ZOOM = ParamScope("ENDLESS_ZOOM")
        val FLUID_SIM = ParamScope("FLUID_SIM")
        val EMITTERS = ParamScope("EMITTERS")
        val JOURNEY = ParamScope("JOURNEY")
        val CYMATICS = ParamScope("CYMATICS")

        /**
         * The scope of the control named [paramKey]. A key that is not a scene parameter at all (a panel
         * preference, an envelope field) is [UNIVERSAL]: it belongs to the panel rather than to a style,
         * so it is always live.
         */
        fun of(paramKey: String): ParamScope = ParamScope(StyleCatalogue.scopeOf(paramKey))
    }
}
