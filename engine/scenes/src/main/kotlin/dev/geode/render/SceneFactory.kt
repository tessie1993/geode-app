package dev.geode.render

import dev.geode.render.scene.Scene

/**
 * Builds a fresh [Scene] for an offscreen render.
 *
 * Lives in `render` rather than beside the exporter so the render and scene code has no dependency
 * on `export` — that was the one edge pointing back out of the engine layer.
 */
interface SceneFactory {
    fun create(): Scene
}
