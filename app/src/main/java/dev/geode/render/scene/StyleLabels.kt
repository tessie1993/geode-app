package dev.geode.render.scene

import android.content.res.Resources
import dev.geode.R

/**
 * The names people read for styles and style families. The native catalogue publishes ids only, so the
 * names are string resources Android can translate, keyed here by id. The table is written out rather
 * than looked up by resource name, because the release build drops a string nothing names in code.
 */
private val STYLE_LABELS: Map<String, Int> =
    mapOf(
        "cymatics" to R.string.style_cymatics,
        "chladni_sand" to R.string.style_chladni_sand,
        "bessel_drum" to R.string.style_bessel_drum,
        "harmonograph" to R.string.style_harmonograph,
        "faraday" to R.string.style_faraday,
        "harmonic_shell" to R.string.style_harmonic_shell,
        "caustic_sheet" to R.string.style_caustic_sheet,
        "levitator" to R.string.style_levitator,
        "standing_chamber" to R.string.style_standing_chamber,
        "rosensweig" to R.string.style_rosensweig,
        "kundt_tube" to R.string.style_kundt_tube,
        "silk_web" to R.string.style_silk_web,
        "silk_bloom" to R.string.style_silk_bloom,
        "silk_weave" to R.string.style_silk_weave,
        "silk_shell" to R.string.style_silk_shell,
        "silk_spiral" to R.string.style_silk_spiral,
        "silk_fold" to R.string.style_silk_fold,
        "silk_hyper" to R.string.style_silk_hyper,
        "silk_resonance" to R.string.style_silk_resonance,
        "silk_curl" to R.string.style_silk_curl,
        "silk_pendulum" to R.string.style_silk_pendulum,
        "life_orbium" to R.string.style_life_orbium,
        "life_gyre" to R.string.style_life_gyre,
        "life_helix" to R.string.style_life_helix,
        "life_pulsar" to R.string.style_life_pulsar,
        "life_hydro" to R.string.style_life_hydro,
        "life_bug" to R.string.style_life_bug,
        "life_mitosis" to R.string.style_life_mitosis,
        "life_coral" to R.string.style_life_coral,
        "life_labyrinth" to R.string.style_life_labyrinth,
        "life_worms" to R.string.style_life_worms,
        "acid_tv" to R.string.style_acid_tv,
        "acid_well" to R.string.style_acid_well,
        "acid_kaleid" to R.string.style_acid_kaleid,
        "acid_droste" to R.string.style_acid_droste,
        "acid_prism" to R.string.style_acid_prism,
        "acid_mosh" to R.string.style_acid_mosh,
        "acid_scan" to R.string.style_acid_scan,
        "acid_solar" to R.string.style_acid_solar,
        "acid_mirror" to R.string.style_acid_mirror,
        "acid_smear" to R.string.style_acid_smear,
        "myco_polycephalum" to R.string.style_myco_polycephalum,
        "myco_rivals" to R.string.style_myco_rivals,
        "myco_symbiosis" to R.string.style_myco_symbiosis,
        "myco_predator" to R.string.style_myco_predator,
        "myco_ghosts" to R.string.style_myco_ghosts,
        "myco_circuit" to R.string.style_myco_circuit,
        "myco_silkroad" to R.string.style_myco_silkroad,
        "myco_sporestorm" to R.string.style_myco_sporestorm,
        "myco_capillary" to R.string.style_myco_capillary,
        "myco_frostvein" to R.string.style_myco_frostvein,
        "fluid" to R.string.style_fluid,
        "fluid_ink" to R.string.style_fluid_ink,
        "fluid_oilslick" to R.string.style_fluid_oilslick,
        "fluid_neon" to R.string.style_fluid_neon,
        "fluid_chrome" to R.string.style_fluid_chrome,
        "fluid_smoke" to R.string.style_fluid_smoke,
        "fluid_lava" to R.string.style_fluid_lava,
        "fluid_marble" to R.string.style_fluid_marble,
        "fluid_aurora" to R.string.style_fluid_aurora,
    )

private val FAMILY_LABELS: Map<String, Int> =
    mapOf(
        "arbor" to R.string.style_family_arbor,
        "bulb" to R.string.style_family_bulb,
        "chrysanthemum" to R.string.style_family_chrysanthemum,
        "lucid" to R.string.style_family_lucid,
        "dive" to R.string.style_family_dive,
        "glass" to R.string.style_family_glass,
        "nebula" to R.string.style_family_nebula,
        "terrain" to R.string.style_family_terrain,
        "truchet" to R.string.style_family_truchet,
        "foam" to R.string.style_family_foam,
        "milkdrop" to R.string.style_family_milkdrop,
    )

/** A style's display name: its string resource when it has one, else the id in words (`neon_city` is "Neon City"). */
fun styleLabel(
    resources: Resources,
    id: String,
): String {
    val label = STYLE_LABELS[id]
    if (label != null) return resources.getString(label)
    return id.split('_').joinToString(" ") { word -> word.replaceFirstChar { it.uppercase() } }
}

/** A Styles page family's display name: its string resource when it has one, else the catalogue's name capitalised. */
fun styleFamilyLabel(
    resources: Resources,
    name: String,
): String {
    val label = FAMILY_LABELS[name]
    if (label != null) return resources.getString(label)
    return name.replaceFirstChar { it.uppercase() }
}
