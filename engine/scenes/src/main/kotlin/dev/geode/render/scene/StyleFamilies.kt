package dev.geode.render.scene

/**
 * Twelve fragment-shader styles that share one GLSL library and one idea, each with its own
 * geometry, camera and way of hearing the music (see GLOSSARY "Style family").
 *
 * A family is not a renderer: every style in it is a plain [SceneKind.SHADER] style, compiled by
 * the native ShaderScene from `shaders/<id>_frag.glsl`, reacting through the same uniforms and
 * the same reaction DNA as any loose shader style. What the family adds is the grouping the
 * picker shows, the shared `lib_<family>.glsl` include, and the free-tier rule: [freeId], the
 * first and most basic style, is free so a free user can try every family; [proIds] are Pro.
 *
 * Every style reads only the standard scene-param uniforms (speed, morph, turbulence, warp,
 * intensity, zoom, the palette), so the Customize macros reach all of them without a per-style
 * table.
 */
data class StyleFamily(
    /** Stable lowercase key, used in picker keys. */
    val name: String,
    /** The heading the picker shows. */
    val label: String,
    /** Every id in the family starts with this. */
    val prefix: String,
    /** The shared include, on the native allow-list in core/viz/ShaderSource.cpp. */
    val library: String,
    /** The styles, the free one first, in the order the picker lists them. */
    val ids: List<String>,
    /** The styles that raymarch, and so spend the Detail budget ([SceneCapabilities.MARCHED_SCENES]). */
    val marchedIds: Set<String>,
) {
    /** The family's free style: the first and most basic. */
    val freeId: String get() = ids.first()

    /** Everything but [freeId]: Pro. */
    val proIds: List<String> get() = ids.drop(1)
}

object StyleFamilies {
    /** Fractal trees: branching folded distance fields, flown through or orbited. */
    val ARBOR: StyleFamily =
        StyleFamily(
            name = "arbor",
            label = "Arbor",
            prefix = "arbor_",
            library = "lib_arbor",
            ids =
                listOf(
                    "arbor_grove",
                    "arbor_willow",
                    "arbor_coral",
                    "arbor_frost",
                    "arbor_bolt",
                    "arbor_roots",
                    "arbor_bonsai",
                    "arbor_canopy",
                    "arbor_rosette",
                    "arbor_spire",
                    "arbor_synapse",
                    "arbor_sakura",
                ),
            marchedIds =
                setOf(
                    "arbor_willow",
                    "arbor_coral",
                    "arbor_bonsai",
                    "arbor_canopy",
                    "arbor_spire",
                    "arbor_synapse",
                ),
        )

    /** Ray-marched power-N Mandelbulbs and their Julia relatives. */
    val BULB: StyleFamily =
        StyleFamily(
            name = "bulb",
            label = "Bulb",
            prefix = "bulb_",
            library = "lib_bulb",
            ids =
                listOf(
                    "bulb_classic",
                    "bulb_julia",
                    "bulb_slice",
                    "bulb_lotus",
                    "bulb_ink",
                    "bulb_glow",
                    "bulb_cavern",
                    "bulb_shell",
                    "bulb_prism",
                    "bulb_contour",
                    "bulb_chrome",
                    "bulb_hybrid",
                ),
            marchedIds =
                setOf(
                    "bulb_classic",
                    "bulb_julia",
                    "bulb_slice",
                    "bulb_lotus",
                    "bulb_ink",
                    "bulb_glow",
                    "bulb_cavern",
                    "bulb_shell",
                    "bulb_prism",
                    "bulb_contour",
                    "bulb_chrome",
                    "bulb_hybrid",
                ),
        )

    /** Polar petal fields and mandalas that bloom out of a glowing core. */
    val CHRYSANTHEMUM: StyleFamily =
        StyleFamily(
            name = "chrysanthemum",
            label = "Chrysanthemum",
            prefix = "chrys_",
            library = "lib_chrys",
            ids =
                listOf(
                    "chrys_bloom",
                    "chrys_phyllo",
                    "chrys_dahlia",
                    "chrys_lotus",
                    "chrys_meadow",
                    "chrys_corolla",
                    "chrys_glass",
                    "chrys_ink",
                    "chrys_neon",
                    "chrys_fall",
                    "chrys_kaleido",
                    "chrys_tunnel",
                ),
            marchedIds =
                setOf(
                    "chrys_corolla",
                ),
        )

    /** Nested domain warps: a noise field sampled through itself, often folded into a kaleidoscope. */
    val LUCID: StyleFamily =
        StyleFamily(
            name = "lucid",
            label = "Lucid",
            prefix = "lucid_",
            library = "lib_lucid",
            ids =
                listOf(
                    "lucid_weave",
                    "lucid_mandala",
                    "lucid_spiral",
                    "lucid_tunnel",
                    "lucid_disc",
                    "lucid_hive",
                    "lucid_ink",
                    "lucid_mirror",
                    "lucid_bulge",
                    "lucid_petal",
                    "lucid_lattice",
                    "lucid_vortex",
                ),
            marchedIds = emptySet(),
        )

    /** Fly-through dives along five camera paths through lattices, jewels, mushrooms, gyroscopes and mandalas. */
    val DIVE: StyleFamily =
        StyleFamily(
            name = "dive",
            label = "Dive",
            prefix = "dive_",
            library = "lib_dive",
            ids =
                listOf(
                    "dive_lattice",
                    "dive_jewel",
                    "dive_mushroom",
                    "dive_gyro",
                    "dive_mandala",
                    "dive_serpent",
                    "dive_helix",
                    "dive_accordion",
                    "dive_orbit",
                    "dive_neon",
                    "dive_cycle",
                    "dive_show",
                ),
            marchedIds =
                setOf(
                    "dive_lattice",
                    "dive_jewel",
                    "dive_mushroom",
                    "dive_gyro",
                    "dive_mandala",
                    "dive_serpent",
                    "dive_helix",
                    "dive_accordion",
                    "dive_orbit",
                    "dive_neon",
                    "dive_cycle",
                    "dive_show",
                ),
        )

    /** Ray-marched heightfield landscapes: ridges, dunes, canyons and islands, flown over. */
    val TERRAIN: StyleFamily =
        StyleFamily(
            name = "terrain",
            label = "Terrain",
            prefix = "terra_",
            library = "lib_terra",
            ids =
                listOf(
                    "terra_ridge",
                    "terra_dunes",
                    "terra_canyon",
                    "terra_glacier",
                    "terra_isles",
                    "terra_mesa",
                    "terra_volcano",
                    "terra_terraces",
                    "terra_moon",
                    "terra_night",
                    "terra_topo",
                    "terra_wire",
                ),
            marchedIds =
                setOf(
                    "terra_ridge",
                    "terra_dunes",
                    "terra_canyon",
                    "terra_glacier",
                    "terra_isles",
                    "terra_mesa",
                    "terra_volcano",
                    "terra_terraces",
                    "terra_moon",
                    "terra_night",
                    "terra_wire",
                ),
        )

    /** In picker order. */
    val all: List<StyleFamily> = listOf(ARBOR, BULB, CHRYSANTHEMUM, LUCID, DIVE, TERRAIN)

    /** Every family style id, in picker order. */
    val ids: List<String> = all.flatMap { it.ids }

    val marchedIds: Set<String> = all.flatMapTo(mutableSetOf()) { it.marchedIds }

    /** The styles a free user cannot pick: all but the first of each family. */
    val proIds: Set<String> = all.flatMapTo(mutableSetOf()) { it.proIds }

    fun of(id: String): StyleFamily? = all.firstOrNull { id in it.ids }
}
