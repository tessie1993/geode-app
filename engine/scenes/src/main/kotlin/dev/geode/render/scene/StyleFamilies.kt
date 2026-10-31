package dev.geode.render.scene

/**
 * Twelve fragment-shader styles that share one GLSL library and one idea, each with its own
 * geometry, camera and way of hearing the music (see GLOSSARY "Style family").
 *
 * A family is not a renderer: every style in it is a plain [SceneKind.SHADER] style, compiled by
 * the native ShaderScene from `shaders/<id>_frag.glsl`, reacting through the same uniforms and
 * the same reaction DNA as any loose shader style. What the family adds is the grouping the
 * picker shows and the shared `lib_<family>.glsl` include. Which styles a family holds, and which
 * one opens it, is the native catalogue's ([Catalogue.families]); what a free user gets of it is
 * `pro/FreeTier`'s.
 */
data class StyleFamily(
    /** Stable lowercase key, used in picker keys and for the [label]. */
    val name: String,
    /** Every id in the family starts with this. */
    val prefix: String,
    /** The shared include, on the native allow-list in core/viz/ShaderSource.cpp. */
    val library: String,
    /** The styles, the opener first, in the order the picker lists them. */
    val ids: List<String>,
) {
    /** The family's first and most basic style. */
    val opener: String get() = ids.first()

    /** The heading the picker shows. */
    val label: String get() = VisualStyleCatalog.familyLabel(name)
}

object StyleFamilies {
    /** In picker order. */
    val all: List<StyleFamily> = Catalogue.families

    /** Every family style id, in picker order. */
    val ids: List<String> = all.flatMap { it.ids }

    fun of(id: String): StyleFamily? = all.firstOrNull { id in it.ids }
}
