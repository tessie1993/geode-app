package dev.geode.render.scene

import dev.geode.engine.scenes.R

/** Which scene class a style id builds. One entry per renderer, not per style. */
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

    BEAM,
}

object SceneCapabilities {
    val SHADER_SCENES: Map<String, Int> =
        linkedMapOf(
            SceneIds.JULIA to R.raw.julia_frag,
            SceneIds.TUNNEL to R.raw.tunnel_frag,
            SceneIds.MANDEL to R.raw.mandel_frag,
            SceneIds.KALEIDO to R.raw.kaleido_frag,
            SceneIds.PLASMA to R.raw.plasma_frag,
            SceneIds.BARS to R.raw.bars_frag,
            SceneIds.RING to R.raw.ring_frag,
            SceneIds.SCOPE to R.raw.scope_frag,
            SceneIds.LISS to R.raw.liss_frag,
            SceneIds.WARP to R.raw.warp_frag,
            SceneIds.GRID to R.raw.grid_frag,
            SceneIds.VORONOI to R.raw.voronoi_frag,
            SceneIds.METABALLS to R.raw.metaballs_frag,
            SceneIds.RIPPLES to R.raw.ripples_frag,
            SceneIds.STARFIELD to R.raw.starfield_frag,
            SceneIds.WAVES to R.raw.waves_frag,
            SceneIds.HEXGRID to R.raw.hexgrid_frag,
            SceneIds.SPIRAL to R.raw.spiral_frag,
            SceneIds.AURORA to R.raw.aurora_frag,
            SceneIds.SOLAR to R.raw.solar_frag,
            SceneIds.WINTER to R.raw.winter_frag,
            SceneIds.LAVA to R.raw.lava_frag,
            // The five styles built on the shared GLSL libraries (lib_scene_uniforms,
            // lib_scene_grade, lib_sdf3, lib_touch) rather than on their own copy of
            // the boilerplate. Same ShaderScene, same uniform contract.
            SceneIds.VANISHING to R.raw.vanishing_frag,
            SceneIds.MORPHOGEN to R.raw.morphogen_frag,
            SceneIds.NEBULA to R.raw.nebula_frag,
            SceneIds.NONEUCLID to R.raw.noneuclid_frag,
            SceneIds.KIFS to R.raw.kifs_frag,
        )

    /**
     * The fragment styles that raymarch, and so spend the [MarchBudget] the Detail control sets.
     *
     * A per-STYLE set rather than a [SceneKind], because only five of the 27 shader styles march;
     * scoping Detail to `SceneKind.SHADER` would put a dead slider in front of anyone looking at
     * Plasma, which is exactly what [ParamScope]'s no-dead-controls rule exists to prevent.
     */
    val MARCHED_SCENES: Set<String> =
        setOf(
            SceneIds.VANISHING,
            SceneIds.MORPHOGEN,
            SceneIds.NEBULA,
            SceneIds.NONEUCLID,
            SceneIds.KIFS,
        )

    /**
     * The renderer [sceneId] builds.
     *
     * Unknown ids resolve to [SceneKind.SILK] because [SceneIds.DEFAULT] is a Silk style and
     * `SceneRegistry` falls back to it, so the Customize panel describes what will actually be
     * on screen rather than an id nothing can render.
     */
    fun kindOf(sceneId: String): SceneKind =
        when {
            sceneId in SHADER_SCENES -> SceneKind.SHADER
            sceneId == SceneIds.MILKDROP -> SceneKind.MILKDROP
            sceneId == SceneIds.FLUID -> SceneKind.FLUID
            sceneId == SceneIds.CURLFLOW -> SceneKind.CURL_FLOW
            sceneId == SceneIds.WATER -> SceneKind.WATER
            sceneId == SceneIds.BEAM -> SceneKind.BEAM
            VisualStyleCatalog.isCymatics(sceneId) -> SceneKind.CYMATICS
            VisualStyleCatalog.life(sceneId) != null -> SceneKind.LIFE
            VisualStyleCatalog.myco(sceneId) != null -> SceneKind.MYCELIUM
            VisualStyleCatalog.acid(sceneId) != null -> SceneKind.ACID
            else -> SceneKind.SILK
        }

    fun isFluid(sceneId: String): Boolean = sceneId == SceneIds.FLUID

    fun isWater(sceneId: String): Boolean = sceneId == SceneIds.WATER

    fun isBeam(sceneId: String): Boolean = sceneId == SceneIds.BEAM

    fun isCymatics(sceneId: String): Boolean = VisualStyleCatalog.isCymatics(sceneId)

    fun hasJourney(sceneId: String): Boolean = sceneId == SceneIds.FLUID || sceneId == SceneIds.CURLFLOW || sceneId == SceneIds.WATER

    fun hasEmitters(sceneId: String): Boolean = sceneId == SceneIds.FLUID || sceneId == SceneIds.WATER

    fun hasParticleLayer(sceneId: String): Boolean = sceneId == SceneIds.FLUID || sceneId == SceneIds.CURLFLOW

    fun usesPointSprites(sceneId: String): Boolean = hasParticleLayer(sceneId)

    fun hasShaderLook(sceneId: String): Boolean = sceneId in SHADER_SCENES
}
