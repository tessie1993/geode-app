package dev.geode.render.bridge

import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.SceneParams
import java.lang.reflect.Field

/**
 * Packs [SceneParams] into the `geode_viz_set_params` frame and back. The frame is the native params
 * schema ([ParamSchema], core/viz/ParamTable.inc): slot i carries the field that schema row i names.
 * Each field is found by that name (app/proguard-rules.pro keeps the names), so no Kotlin list holds
 * the order and nothing here can fall out of step with the table: a row the data class lacks fails
 * the codec's first use loudly instead of shifting every slot after it. A retired row has no field:
 * its slot carries the row's default out and is skipped coming back. Ints and flags travel as floats.
 */
object SceneParamsCodec {
    private val SLOTS: Array<Field?> = slotFields()

    /** The frame's float count: one per schema row. */
    val FIELDS: Int = SLOTS.size

    fun pack(
        p: SceneParams,
        out: FloatArray,
    ) {
        check(out.size >= FIELDS) { "scene parameter frame needs $FIELDS floats, got ${out.size}" }
        SLOTS.forEachIndexed { slot, field ->
            out[slot] =
                when (field?.type) {
                    null -> ParamSchema[slot].default
                    Boolean::class.javaPrimitiveType -> if (field.getBoolean(p)) 1f else 0f
                    Int::class.javaPrimitiveType -> field.getInt(p).toFloat()
                    else -> field.getFloat(p)
                }
        }
    }

    /** [p] as a fresh frame. */
    fun pack(p: SceneParams): FloatArray = FloatArray(FIELDS).also { pack(p, it) }

    /**
     * The inverse of [pack]: a frame back into parameters, ints rounded and flags over 0.5. The two
     * Kotlin-only fields (the saved palette slots' ids, [SceneParams.NOT_RENDERED]) come from [base];
     * an id is dropped when the frame cleared the hue overrides it stands for, since a saved palette
     * IS its overrides (see [SceneParams.withoutCustomPalette]).
     */
    fun unpack(
        values: FloatArray,
        base: SceneParams,
    ): SceneParams {
        check(values.size >= FIELDS) { "scene parameter frame needs $FIELDS floats, got ${values.size}" }
        val out = base.copy()
        SLOTS.forEachIndexed { slot, field ->
            val v = values[slot]
            when (field?.type) {
                null -> Unit
                Float::class.javaPrimitiveType -> field.setFloat(out, v)
                Int::class.javaPrimitiveType -> field.setInt(out, Math.round(v))
                Boolean::class.javaPrimitiveType -> field.setBoolean(out, v > 0.5f)
                else -> error("${field?.name} is not a wire type")
            }
        }
        var result = out
        if (!result.usesCustomPalette) result = result.copy(customPaletteId = SceneParams.NO_CUSTOM_PALETTE)
        if (!result.usesCustomPalette2) result = result.copy(customPalette2Id = SceneParams.NO_CUSTOM_PALETTE)
        return result
    }

    /** The [SceneParams] field of every schema row, in slot order; null for a retired row. */
    private fun slotFields(): Array<Field?> {
        val byName = SceneParams::class.java.declaredFields.associateBy { it.name }
        return Array(ParamSchema.params.size) { slot ->
            val spec = ParamSchema[slot]
            if (spec.retired) return@Array null
            checkNotNull(byName[spec.name]) { "native scene param ${spec.name} has no SceneParams field" }.apply { isAccessible = true }
        }
    }
}
