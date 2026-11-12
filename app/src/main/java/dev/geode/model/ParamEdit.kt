package dev.geode.model

import dev.geode.render.bridge.SceneParamsCodec
import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.SceneParams
import kotlin.math.roundToInt

/*
 * Scene params by name, so Customize edits a param by its schema row's name (core/viz/ParamTable.inc) rather than
 * through a Kotlin property. A name finds its wire slot in [ParamSchema]; reads and writes go through the wire
 * codec. Ranges, clamps and defaults stay native. Temporary (#1150): these become [SceneParams]' own members.
 */

private fun slotOf(name: String): Int {
    val spec = ParamSchema[name]
    require(spec != null && !spec.retired) { "no live scene param named $name in the params schema" }
    return spec.slot
}

private val defaultFrame: FloatArray by lazy { SceneParamsCodec.pack(SceneParams.DEFAULT) }

// A panel reads many names from one look per recomposition, so the last look's frame is kept; it is never written.
@Volatile
private var lastRead: Pair<SceneParams, FloatArray>? = null

private fun frameOf(p: SceneParams): FloatArray {
    if (p === SceneParams.DEFAULT) return defaultFrame
    lastRead?.let { (seen, frame) -> if (seen === p) return frame }
    return SceneParamsCodec.pack(p).also { lastRead = p to it }
}

/** Param [name] as the wire carries it (an int or a flag as a float); throws for a name the schema lacks. */
operator fun SceneParams.get(name: String): Float = frameOf(this)[slotOf(name)]

/** Whole-number param [name]. */
fun SceneParams.int(name: String): Int = get(name).roundToInt()

/** On/off param [name]. */
fun SceneParams.flag(name: String): Boolean = get(name) > 0.5f

/** `copy(name = value)`: these params with [name] set, the saved palette's id kept as `copy` keeps it. */
fun SceneParams.with(
    name: String,
    value: Float,
): SceneParams {
    val frame = frameOf(this).copyOf()
    frame[slotOf(name)] = value
    return SceneParamsCodec.unpack(frame, this).copy(customPaletteId = customPaletteId)
}
