package dev.geode.model

/**
 * The Styles page's groups: what a style looks like, not which engine draws it.
 *
 * WHY by look: chips named after engines and families ("Shaders", "Mycelium", "Truchet") told a
 * music lover nothing, and "Shaders" held sixty unrelated looks. People browse visualizers by
 * picture and feeling (docs/ui-redesign; the owner chose these groups on 2026-11-02). The engine's
 * kinds and families stay searchable through [StyleEntry.kindLabel] and [StyleEntry.familyLabel].
 *
 * This is a first draft of the tags for the owner to correct: kinds and families map whole, and
 * the loose shader styles are placed one by one in [LOOSE]. A style missing from every rule lands
 * in [PSYCHEDELIC], so a new style always shows somewhere.
 */
enum class StyleLook(
    val label: String,
) {
    FLOWING("Flowing & liquid"),
    LIVING("Living"),
    PSYCHEDELIC("Psychedelic"),
    FRACTALS("Fractals"),
    SPACE("Space"),
    FLY_THROUGH("Fly-through"),
    GLASS("Glass & light"),
    PATTERNS("Patterns"),
    CLASSIC("Classic"),
    ;

    companion object {
        /**
         * The group of style [id], whose engine kind is [kindName] (`SceneKind.name`). Family
         * styles go by their family prefix, other kinds by kind, loose shaders by [LOOSE].
         */
        fun of(
            id: String,
            kindName: String,
        ): StyleLook =
            FAMILIES.entries.firstOrNull { (prefix, _) -> id.startsWith(prefix) }?.value
                ?: KINDS[kindName]
                ?: LOOSE[id]
                ?: PSYCHEDELIC

        private val FAMILIES =
            mapOf(
                "bulb_" to FRACTALS,
                "arbor_" to FRACTALS,
                "foam_" to FRACTALS,
                "dive_" to FLY_THROUGH,
                "terra_" to FLY_THROUGH,
                "nebula_" to SPACE,
                "glass_" to GLASS,
                "truchet_" to PATTERNS,
                "lucid_" to PSYCHEDELIC,
                "chrys_" to PSYCHEDELIC,
            )

        private val KINDS =
            mapOf(
                "SILK" to FLOWING,
                "FLUID" to FLOWING,
                "CURL_FLOW" to FLOWING,
                "WATER" to FLOWING,
                "LIFE" to LIVING,
                "MYCELIUM" to LIVING,
                "ACID" to PSYCHEDELIC,
                "CYMATICS" to PATTERNS,
                "MILKDROP" to CLASSIC,
            )

        private val LOOSE: Map<String, StyleLook> =
            buildMap {
                listOf("julia", "mandel", "kifs", "fractal_temple", "menger_core", "mandelbox_cathedral", "supershape")
                    .forEach { put(it, FRACTALS) }
                listOf(
                    "tunnel",
                    "rod_tunnel",
                    "particle_tunnel",
                    "fractal_dive",
                    "fractal_tunnel",
                    "outrun_drive",
                    "vanishing",
                    "crystal_canyon",
                    "spectrum_terrain",
                    "gyroid_cavern",
                    "noneuclid",
                ).forEach { put(it, FLY_THROUGH) }
                listOf("starfield", "nebula", "solar", "chrome_nebula", "ringed_planet", "black_hole", "aurora", "aurora_ocean")
                    .forEach { put(it, SPACE) }
                listOf(
                    "orb_lattice",
                    "neon_tiles",
                    "chroma_orb",
                    "blacklight_bloom",
                    "prism_rain",
                    "glow_vortex",
                    "neon_chrome_sea",
                    "neon_city",
                    "merkaba_grid",
                    "bead_vortex",
                ).forEach { put(it, GLASS) }
                listOf("kaleido", "grid", "voronoi", "hexgrid", "spiral", "mandala_dome", "hex_hive", "spiral_eye")
                    .forEach { put(it, PATTERNS) }
                listOf("bars", "ring", "scope", "liss", "waves").forEach { put(it, CLASSIC) }
                listOf(
                    "plasma",
                    "warp",
                    "metaballs",
                    "lava",
                    "morphogen",
                    "curl_bloom",
                    "nectar_flow",
                    "jelly_abyss",
                    "ribbon_storm",
                    "ripples",
                ).forEach { put(it, FLOWING) }
            }
    }
}
