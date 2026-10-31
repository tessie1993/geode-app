package dev.geode.render.scene

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.SceneParamsCodec
import kotlin.random.Random

/**
 * The Customize panel's Randomize and Reset-tab buttons, asked of the engine.
 *
 * What each control rolls to, which tab owns it, which fields it writes and which styles it can
 * move are native (core/viz/ParamRandomizer, ADR 0003), so a roll is the same maths wherever it
 * is asked from. This object only packs [SceneParams] into the wire frame, passes a seed from the
 * caller's [Random], and unpacks the answer. Controls are named by their [ParamKeys] strings: the
 * label on screen is also the key the user locks by.
 */
object ParamRandomizer {
    /** The controls that offer a lock: every one a roll can move (`TAB|key` lines from the engine). */
    val LOCKABLE_LABELS: Set<String> by lazy {
        GeodeNative
            .paramsRollKeys()
            .lines()
            .filter { it.isNotBlank() }
            .mapTo(mutableSetOf()) { it.substringAfter('|') }
    }

    /**
     * Rolls a new look.
     *
     * [sceneId] scopes the roll to what the style on screen can actually show: rolling `Morph` on
     * a fluid style or `Fluid curl` on a shader burns the roll on a parameter the user cannot see
     * and cannot feel, which is how "randomize" ends up appearing to do nothing. Null rolls the
     * whole surface; a null [tab] rolls every tab. Returns [current] itself when nothing could change.
     */
    fun randomize(
        current: SceneParams,
        locked: Set<String>,
        rng: Random = Random.Default,
        tab: CustomizeTab? = null,
        sceneId: String? = null,
    ): SceneParams {
        val out = FloatArray(SceneParamsCodec.FIELDS)
        val changed =
            GeodeNative.paramsRandomize(
                styleId = sceneId,
                tab = tab?.name,
                locked = locked.joinToString("\n"),
                seed = rng.nextLong(),
                input = SceneParamsCodec.pack(current),
                output = out,
            )
        return if (changed) SceneParamsCodec.unpack(out, current) else current
    }

    /** Puts one tab's parameters back to their defaults and leaves every other tab alone. */
    fun resetTab(
        current: SceneParams,
        tab: CustomizeTab,
    ): SceneParams {
        val out = FloatArray(SceneParamsCodec.FIELDS)
        GeodeNative.paramsResetTab(tab.name, SceneParamsCodec.pack(current), out)
        return SceneParamsCodec.unpack(out, current)
    }
}
