package dev.geode.model

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.scene.SceneParams
import kotlin.reflect.KProperty1

/**
 * The range Customize's slider offers for a scene param. Native decides it (core/viz/ControlRange,
 * `geode_params_control_range`): the params schema's range, cut short only where a control stops early
 * on purpose, so no two controls for one param disagree and none leaves the range the engine clamps to.
 *
 * Each answer is kept by name: the sliders ask on every recomposition, and a range never changes while
 * the process runs.
 */
object ControlRange {
    private val ranges = HashMap<String, ClosedFloatingPointRange<Float>>()

    /**
     * The control range of [param]. The property reference, not a string, makes a misspelt param a
     * compile error; a [SceneParams] field the schema lacks throws, as its control would have no range.
     */
    fun of(param: KProperty1<SceneParams, *>): ClosedFloatingPointRange<Float> = of(param.name)

    /** [of] by the param's name (its Kotlin property name); throws for a name the schema lacks. */
    fun of(name: String): ClosedFloatingPointRange<Float> =
        ranges.getOrPut(name) {
            val out = FloatArray(2)
            require(GeodeNative.paramsControlRange(name, out)) { "no scene param named $name in the params schema" }
            out[0]..out[1]
        }
}
