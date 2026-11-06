package dev.geode.ui

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.Category
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.Psychology
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material.icons.filled.Waves
import androidx.compose.ui.graphics.vector.ImageVector
import dev.geode.engine.bridge.GeodeNative
import java.io.File

/**
 * Customize's ten categories, in panel order, by what their controls do. The names are
 * core/viz/CustomizeMap's; everything about a category but its words and icon is native: which
 * sections and controls it has, which are free (Basics), which styles it shows on, what its
 * Randomize rolls and what its Reset puts back. Kotlin draws it.
 */
enum class CustomizeCategory(
    val title: String,
    val summary: String,
    val icon: ImageVector,
) {
    QUICK_KNOBS("Quick knobs", "Six knobs that change the whole look", Icons.Filled.Tune),
    LOOK("Look", "Palette, brightness, colour and glow", Icons.Filled.Palette),
    SHAPE("Shape", "Morph, symmetry and the style's own form", Icons.Filled.Category),
    MOTION("Motion", "Speed, zoom, spin, drift and the dive", Icons.Filled.Speed),
    CAMERA("Camera", "How the camera flies through the 3D world", Icons.Filled.Videocam),
    MUSIC("Music", "How the music moves the picture", Icons.Filled.MusicNote),
    EFFECTS("Effects", "Trails, lens and film looks", Icons.Filled.AutoAwesome),
    LIQUID("Liquid", "Turn the style into a flowing liquid", Icons.Filled.Waves),
    BEHAVIOUR("Behaviour", "Reaction personality, transitions, modulation", Icons.Filled.Psychology),
    EXPERT("Expert", "The style's engine, layers and shader code", Icons.Filled.Code),
    ;

    companion object {
        fun named(name: String?): CustomizeCategory? = entries.firstOrNull { it.name == name }
    }
}

/** One row of the native map: a category's Basics (always open, free) or a Pro section that drops open. */
internal data class CustomizeSection(
    val category: CustomizeCategory,
    /** Unique inside its category; the open ones are saved by [path]. */
    val id: String,
    val basics: Boolean,
    /** ParamKeys strings, or `@id` for a panel piece that is not one parameter. */
    val controls: List<String>,
) {
    val path: String get() = "${category.name}/$id"

    /** The words on the section's header. Basics has none: it is the top of the category. */
    val title: String get() = SECTION_TITLES[id] ?: id.replace('_', ' ').replaceFirstChar { it.uppercase() }
}

/** Section headers, by the native section id (UI words only; the sections themselves are native). */
private val SECTION_TITLES =
    mapOf(
        "basics" to "Basics",
        "sets" to "Sets",
        "randomize" to "Randomize & reset",
        "colour_detail" to "Colour detail",
        "colour_cycle" to "Colour cycle",
        "light_detail" to "Light detail",
        "palette_maker" to "Make a palette",
        "form" to "Form",
        "symmetry" to "Symmetry",
        "particles" to "Particles",
        "journey" to "Journey",
        "emitters" to "Emitters",
        "drift" to "Drift",
        "endless_dive" to "Endless dive",
        "inner_motion" to "Inner motion",
        "framing" to "Framing",
        "feel" to "Feel",
        "moments" to "Moments",
        "listening" to "Listening",
        "notes_key" to "Notes & key",
        "trail_detail" to "Trail detail",
        "lens" to "Lens",
        "film" to "Film",
        "push_swirl" to "Push & swirl",
        "surface" to "Surface",
        "ripples" to "Ripples",
        "water" to "Water",
        "transition_detail" to "Transition detail",
        "reactions" to "Reactions",
        "envelopes" to "Envelopes",
        "waves" to "Waves",
        "engine" to "This style's engine",
        "environment" to "Environment",
        "layers" to "Layers",
        "shader_code" to "Shader code",
    )

/**
 * The native map's rows as the panel draws them (core/viz/CustomizeMap's `text()`). This splits
 * lines into rows and nothing more: no rule about a control is decided here.
 *
 * JVM unit tests cannot load libgeode; the build points [TEXT_FILE_PROPERTY] at
 * `core/tests/customize.txt`, which core/tests/customize_test proves byte-equal to what C++ emits.
 */
internal object CustomizeMapTable {
    /** core/viz/CustomizeMap.hpp's `kTextVersion`. */
    const val VERSION: Int = 1

    const val TEXT_FILE_PROPERTY: String = "geode.customize"

    val sections: List<CustomizeSection> by lazy {
        rows(System.getProperty(TEXT_FILE_PROPERTY)?.let { File(it).readText() } ?: GeodeNative.customizeText())
    }

    fun sectionsOf(category: CustomizeCategory): List<CustomizeSection> = sections.filter { it.category == category }

    private val byControl: Map<String, CustomizeCategory> by lazy {
        sections.flatMap { section -> section.controls.map { it to section.category } }.toMap()
    }

    /** The category the engine files [control] under, or null for a name the map does not have. */
    fun categoryOf(control: String): CustomizeCategory? = byControl[control]

    /** Every control in a Basics section, as the engine says. */
    val basicControls: Set<String> by lazy { sections.filter { it.basics }.flatMapTo(mutableSetOf()) { it.controls } }

    /** What the panel offers on [styleId] (`CATEGORY` and `CATEGORY/id` lines). Native. */
    fun shownOn(styleId: String): Set<String> =
        GeodeNative
            .customizeShown(styleId)
            .lines()
            .filterTo(mutableSetOf()) { it.isNotBlank() }

    fun rows(text: String): List<CustomizeSection> {
        val out = mutableListOf<CustomizeSection>()
        text
            .lineSequence()
            .map { it.trimEnd('\r') }
            .filter { it.isNotBlank() }
            .forEach { line ->
                val f = line.split('|')
                when (f[0]) {
                    "customize" -> check(f.getOrNull(1)?.toIntOrNull() == VERSION) { "Customize map version ${f.getOrNull(1)}" }
                    "section" -> {
                        val category = CustomizeCategory.named(f[1]) ?: return@forEach
                        val controls = f[5].split(',').filter { it.isNotEmpty() }
                        out += CustomizeSection(category, f[2], basics = f[3] == "BASICS", controls = controls)
                    }
                    else -> Unit
                }
            }
        return out
    }
}
