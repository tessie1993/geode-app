package dev.geode.model

import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.SceneParams
import kotlin.reflect.KProperty1

/**
 * The range Customize's slider offers for a scene param: the native params schema's range
 * ([ParamSchema], core/viz/ParamTable.inc), cut short only where [CAPS] says the control stops early.
 * Every slider range for a scene param comes from here (#346), so no two controls for one param
 * disagree and none leaves the range the engine clamps to.
 *
 * It lives in `model` because the editor's keyframe table read it as well (`editor/AnimatableParams`,
 * gone with the Studio project render). The caps are a UI fact, so Kotlin; every range itself is
 * native.
 */
object ControlRange {
    /**
     * Where the slider and the keyframes stop short of the schema's max on purpose; the LFO and the
     * reaction routes still reach the schema's max.
     */
    internal val CAPS: Map<String, Float> =
        mapOf(
            // 1 + the flash clamp's depth: the flash clamp takes back anything brighter, so it never shows.
            "brightness" to 1.25f,
            // As brightness: above 1.25 the flash clamp takes it back.
            "intensity" to 1.25f,
            // The flash clamp's depth: more bloom is clamped away before it reaches the screen.
            "bloom" to 0.25f,
            // The slider stops at 2; 2..2.5 is headroom for the LFO and the routes.
            "camFly" to 2f,
        )

    /** As [CAPS] for the min: Fly runs backward on the slider to -2, and the LFO and routes reach -2.5. */
    internal val FLOORS: Map<String, Float> = mapOf("camFly" to -2f)

    /**
     * The control range of [param]. The property reference, not a string, makes a misspelt param a
     * compile error; a [SceneParams] field the schema lacks throws, as its control would have no range.
     */
    fun of(param: KProperty1<SceneParams, *>): ClosedFloatingPointRange<Float> = of(param.name)

    /** [of] by the param's name (its Kotlin property name); throws for a name the schema lacks. */
    fun of(name: String): ClosedFloatingPointRange<Float> {
        val range = requireNotNull(ParamSchema[name]) { "no scene param named $name in the params schema" }.range
        return maxOf(FLOORS[name] ?: range.start, range.start)..minOf(CAPS[name] ?: range.endInclusive, range.endInclusive)
    }
}
