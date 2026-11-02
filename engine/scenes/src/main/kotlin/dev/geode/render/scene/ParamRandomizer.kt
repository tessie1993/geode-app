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
 *
 * The panel's pages (Look, Style, Advanced) are not the engine's groups: each spans several, and
 * hands a few controls to another page ([CustomizeTab.gives], [CustomizeTab.takes]). A roll of a
 * page holds every control the page does not own, the same way the engine holds a lock, so no new
 * native entry point was needed.
 */
object ParamRandomizer {
    /** The engine's roll table as `group -> key` pairs, in roll order. */
    val ROLL_KEYS: List<Pair<String, String>> by lazy {
        GeodeNative
            .paramsRollKeys()
            .lines()
            .filter { it.isNotBlank() }
            .map { it.substringBefore('|') to it.substringAfter('|') }
    }

    /** The controls that offer a lock: every one a roll can move (`TAB|key` lines from the engine). */
    val LOCKABLE_LABELS: Set<String> by lazy { ROLL_KEYS.mapTo(mutableSetOf()) { it.second } }

    /**
     * The wire fields of the controls a panel page moves across engine groups. Only those need
     * naming here: every other control is reset with its whole engine group, which the engine
     * already knows field by field.
     */
    private val MOVED_FIELDS: Map<String, List<String>> =
        mapOf(
            ParamKeys.CAM_PATH to listOf("camPath"),
            ParamKeys.CAM_FLY to listOf("camFly"),
            ParamKeys.CAM_ORBIT to listOf("camOrbit"),
            ParamKeys.CAM_FOV to listOf("camFov"),
            ParamKeys.CAM_SHAKE to listOf("camShake"),
            ParamKeys.ENDLESS_ZOOM to listOf("endlessZoom"),
            ParamKeys.DIVE_SPEED to listOf("endlessZoomSpeed"),
            ParamKeys.TURBULENCE to listOf("turbulence"),
            ParamKeys.FLOW_STRENGTH to listOf("flowStrength"),
            ParamKeys.FLOW_FORCE to listOf("flowForce"),
            ParamKeys.FLOW_CURL to listOf("flowCurl"),
            // Liquid mode's pickers are never rolled (native leaves them out of the roll table), so
            // only a page reset or a hand-off between pages ever moves them.
            ParamKeys.LIQUID_BORN to listOf("liquidBorn"),
            ParamKeys.LIQUID_AGE to listOf("liquidAge"),
            ParamKeys.LIQUID_SURFACE to listOf("liquidSurface"),
            ParamKeys.RIPPLE_OVERLAY_STRENGTH to listOf("rippleOverlayStrength"),
            ParamKeys.RIPPLE_GLINT to listOf("rippleOverlaySpecular"),
        )

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
        val engineGroup = tab?.takeIf { it.groups == listOf(it) }
        val held =
            if (tab == null || engineGroup != null) {
                locked
            } else {
                val owned = tab.ownedKeys(ROLL_KEYS)
                if (owned.isEmpty()) return current
                locked + (LOCKABLE_LABELS - owned)
            }
        val out = FloatArray(SceneParamsCodec.FIELDS)
        val changed =
            GeodeNative.paramsRandomize(
                styleId = sceneId,
                tab = engineGroup?.name,
                locked = held.joinToString("\n"),
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
        val before = SceneParamsCodec.pack(current)
        var out = before
        tab.groups.forEach { group -> out = resetGroup(group, out) }
        tab.gives.forEach { key -> copyFields(key, from = before, into = out) }
        tab.takes.forEach { key ->
            val home = ROLL_KEYS.firstOrNull { it.second == key }?.first
            val group = CustomizeTab.entries.firstOrNull { it.name == home }
            if (group != null) copyFields(key, from = resetGroup(group, before), into = out)
        }
        return SceneParamsCodec.unpack(out, current)
    }

    private fun resetGroup(
        group: CustomizeTab,
        input: FloatArray,
    ): FloatArray {
        val out = FloatArray(SceneParamsCodec.FIELDS)
        GeodeNative.paramsResetTab(group.name, input, out)
        return out
    }

    private fun copyFields(
        key: String,
        from: FloatArray,
        into: FloatArray,
    ) {
        MOVED_FIELDS[key].orEmpty().forEach { field ->
            val i = SceneParamsCodec.NAMES.indexOf(field)
            if (i >= 0) into[i] = from[i]
        }
    }
}
