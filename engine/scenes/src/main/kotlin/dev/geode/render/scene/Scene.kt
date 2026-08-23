package dev.geode.render.scene

import dev.geode.analysis.AudioFeatures

interface Scene {
    val id: String

    fun init()

    fun setParams(params: SceneParams) {}

    fun resize(
        width: Int,
        height: Int,
    )

    fun update(
        features: AudioFeatures,
        dt: Float,
    )

    fun draw(timeSeconds: Float)

    fun release()
}
