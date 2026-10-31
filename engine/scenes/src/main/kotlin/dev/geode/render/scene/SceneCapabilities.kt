package dev.geode.render.scene

/** Which scene class a style id builds. One entry per renderer, not per style. Names match core/viz/Catalogue's. */
enum class SceneKind {
    SHADER,

    SILK,

    LIFE,

    MYCELIUM,

    ACID,

    MILKDROP,

    FLUID,

    CURL_FLOW,

    WATER,

    CYMATICS,
}

/** What a style can do, asked of the native [Catalogue]. */
object SceneCapabilities {
    /** Fragment style id -> the APK asset the native ShaderScene compiles, loose styles first, then the families. */
    val SHADER_SCENES: Map<String, String> =
        Catalogue.styles
            .filter { it.kind == SceneKind.SHADER }
            .associateTo(linkedMapOf()) { it.id to "shaders/${it.id}_frag.glsl" }

    fun kindOf(sceneId: String): SceneKind = Catalogue.kindOf(sceneId)

    fun isFluid(sceneId: String): Boolean = kindOf(sceneId) == SceneKind.FLUID

    fun isCymatics(sceneId: String): Boolean = kindOf(sceneId) == SceneKind.CYMATICS

    fun hasShaderLook(sceneId: String): Boolean = sceneId in SHADER_SCENES
}
