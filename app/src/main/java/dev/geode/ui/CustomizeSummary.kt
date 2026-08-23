package dev.geode.ui

import dev.geode.data.PresetStore
import dev.geode.render.scene.SceneParams
import org.json.JSONObject

object CustomizeSummary {
    private val defaults: JSONObject by lazy { paramsJson(SceneParams.DEFAULT) }

    fun changedCount(params: SceneParams): Int {
        if (params == SceneParams.DEFAULT) return 0
        val current = paramsJson(params)
        var changed = 0
        for (key in current.keys()) {
            if (current.opt(key) != defaults.opt(key)) changed++
        }
        return changed
    }

    private fun paramsJson(params: SceneParams): JSONObject = PresetStore.paramsToJson(params)
}
