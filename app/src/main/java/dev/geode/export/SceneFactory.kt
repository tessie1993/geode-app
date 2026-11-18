package dev.geode.export

/** What an offscreen render needs to know about the scene it draws (`Stage.exportSceneFactory`). */
interface SceneFactory {
    val sceneId: String
}
