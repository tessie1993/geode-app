package dev.geode.render.scene

/**
 * The style pickers' lists and the display labels behind them.
 *
 * The ids, their order and which renderer builds each one are the native catalogue's
 * ([Catalogue]); so are the per-style tuning numbers (core/viz/scenes/StyleCatalog). What lives
 * here is only what the shell shows: a label per id, kept in Kotlin so it can be translated.
 * A style without an entry (the loose shader styles) is labelled from its id by the picker.
 */
object VisualStyleCatalog {
    private val LABELS: Map<String, String> =
        mapOf(
            "cymatics" to "Original · Resonant Field",
            "chladni_sand" to "Chladni Sand",
            "bessel_drum" to "Drumhead",
            "harmonograph" to "Harmonograph",
            "faraday" to "Faraday",
            "harmonic_shell" to "Harmonic Shell",
            "caustic_sheet" to "Caustic Sheet",
            "levitator" to "Levitator",
            "standing_chamber" to "Standing Chamber",
            "rosensweig" to "Rosensweig Spikes",
            "kundt_tube" to "Kundt Tube",
            "silk_web" to "Halvorsen Web",
            "silk_bloom" to "Cosine Bloom",
            "silk_weave" to "Triaxial Weave",
            "silk_shell" to "Concentric Shells",
            "silk_spiral" to "Phase Spiral",
            "silk_fold" to "Recursive Fold",
            "silk_hyper" to "Hyperbolic Bloom",
            "silk_resonance" to "Nested Resonance",
            "silk_curl" to "Curl Weave",
            "silk_pendulum" to "Pendulum Garden",
            "life_orbium" to "Orbium Drift",
            "life_gyre" to "Gyre Garden",
            "life_helix" to "Helicium Reef",
            "life_pulsar" to "Pulsar Colony",
            "life_hydro" to "Hydrogeminium",
            "life_bug" to "Smooth Bugs",
            "life_mitosis" to "Mitosis",
            "life_coral" to "Coral Bloom",
            "life_labyrinth" to "Living Ink",
            "life_worms" to "Ember Worms",
            "acid_tv" to "TV Acid",
            "acid_well" to "Phosphor Well",
            "acid_kaleid" to "Kaleido Melt",
            "acid_droste" to "Droste Throat",
            "acid_prism" to "Prism Drift",
            "acid_mosh" to "Datamosh",
            "acid_scan" to "Scanline Surge",
            "acid_solar" to "Solar Flare",
            "acid_mirror" to "Mirror Room",
            "acid_smear" to "Neon Smear",
            "myco_polycephalum" to "Polycephalum",
            "myco_rivals" to "Rival Colonies",
            "myco_symbiosis" to "Symbiosis",
            "myco_predator" to "Predator",
            "myco_ghosts" to "Ghost Veil",
            "myco_circuit" to "Circuit Bloom",
            "myco_silkroad" to "Silk Roads",
            "myco_sporestorm" to "Spore Storm",
            "myco_capillary" to "Capillaries",
            "myco_frostvein" to "Frost Veins",
            "fluid" to "Original · Dye",
            "fluid_ink" to "Ink Wash",
            "fluid_oilslick" to "Oil Slick",
            "fluid_neon" to "Neon Contours",
            "fluid_chrome" to "Liquid Chrome",
            "fluid_smoke" to "Smoke",
            "fluid_lava" to "Lava",
            "fluid_marble" to "Marble",
            "fluid_aurora" to "Aurora",
        )

    private val FAMILY_LABELS: Map<String, String> =
        mapOf(
            "arbor" to "Arbor",
            "bulb" to "Bulb",
            "chrysanthemum" to "Chrysanthemum",
            "lucid" to "Lucid",
            "dive" to "Dive",
            "glass" to "Glass",
            "nebula" to "Nebula",
            "terrain" to "Terrain",
            "truchet" to "Truchet",
            "foam" to "Sphere Foam",
        )

    val cymaticsIds: List<String> = idsOf(SceneKind.CYMATICS)
    val fluidIds: List<String> = idsOf(SceneKind.FLUID)
    val silkIds: List<String> = idsOf(SceneKind.SILK)
    val lifeIds: List<String> = idsOf(SceneKind.LIFE)
    val acidIds: List<String> = idsOf(SceneKind.ACID)
    val mycoIds: List<String> = idsOf(SceneKind.MYCELIUM)

    /** The mycelium style [id], or null when it is not one. */
    fun myco(id: String): CatalogueStyle? = Catalogue.style(id)?.takeIf { it.kind == SceneKind.MYCELIUM }

    fun isCymatics(id: String): Boolean = SceneCapabilities.isCymatics(id)

    /** True for every style that runs the Fluid solver, `fluid` itself included. */
    fun isFluid(id: String): Boolean = SceneCapabilities.isFluid(id)

    fun label(id: String): String = LABELS[id] ?: id

    fun familyLabel(name: String): String = FAMILY_LABELS[name] ?: name.replaceFirstChar { it.uppercase() }

    private fun idsOf(kind: SceneKind): List<String> = Catalogue.styles.filter { it.kind == kind }.map { it.id }
}
