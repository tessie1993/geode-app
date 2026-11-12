package dev.geode.export

/** What an offscreen render needs to know about the scene it draws (`Stage.exportSceneFactory`). */
interface SceneFactory {
    val sceneId: String

    /** The MilkDrop preset the live renderer had loaded, for an export of the milkdrop scene. */
    val milkPresetPath: String?
}
