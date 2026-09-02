package dev.geode.render.scene

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
    /** Fragment style id -> the APK asset the native ShaderScene compiles. */
    val SHADER_SCENES: Map<String, String> =
        linkedMapOf(
            SceneIds.JULIA to "shaders/julia_frag.glsl",
            SceneIds.TUNNEL to "shaders/tunnel_frag.glsl",
            SceneIds.MANDEL to "shaders/mandel_frag.glsl",
            SceneIds.KALEIDO to "shaders/kaleido_frag.glsl",
            SceneIds.PLASMA to "shaders/plasma_frag.glsl",
            SceneIds.BARS to "shaders/bars_frag.glsl",
            SceneIds.RING to "shaders/ring_frag.glsl",
            SceneIds.SCOPE to "shaders/scope_frag.glsl",
            SceneIds.LISS to "shaders/liss_frag.glsl",
            SceneIds.WARP to "shaders/warp_frag.glsl",
            SceneIds.GRID to "shaders/grid_frag.glsl",
            SceneIds.VORONOI to "shaders/voronoi_frag.glsl",
            SceneIds.METABALLS to "shaders/metaballs_frag.glsl",
            SceneIds.RIPPLES to "shaders/ripples_frag.glsl",
            SceneIds.STARFIELD to "shaders/starfield_frag.glsl",
            SceneIds.WAVES to "shaders/waves_frag.glsl",
            SceneIds.HEXGRID to "shaders/hexgrid_frag.glsl",
            SceneIds.SPIRAL to "shaders/spiral_frag.glsl",
            SceneIds.AURORA to "shaders/aurora_frag.glsl",
            SceneIds.SOLAR to "shaders/solar_frag.glsl",
            SceneIds.WINTER to "shaders/winter_frag.glsl",
            SceneIds.LAVA to "shaders/lava_frag.glsl",
            // The five styles built on the shared GLSL libraries (lib_scene_uniforms,
            // lib_scene_grade, lib_sdf3, lib_touch) rather than on their own copy of
            // the boilerplate. Same ShaderScene, same uniform contract.
            SceneIds.VANISHING to "shaders/vanishing_frag.glsl",
            SceneIds.MORPHOGEN to "shaders/morphogen_frag.glsl",
            SceneIds.NEBULA to "shaders/nebula_frag.glsl",
            SceneIds.NONEUCLID to "shaders/noneuclid_frag.glsl",
            SceneIds.KIFS to "shaders/kifs_frag.glsl",
            SceneIds.ORB_LATTICE to "shaders/orb_lattice_frag.glsl",
            SceneIds.ROD_TUNNEL to "shaders/rod_tunnel_frag.glsl",
            SceneIds.NEON_TILES to "shaders/neon_tiles_frag.glsl",
        )

    /**
     * The fragment styles that raymarch, and so spend the [MarchBudget] the Detail control sets.
     *
     * A per-STYLE set rather than a [SceneKind], because only six of the 30 shader styles march;
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
            SceneIds.ROD_TUNNEL,
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
