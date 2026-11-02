package dev.geode.model

/**
 * The Styles page's groups: what a style looks like, not which engine draws it.
 *
 * WHY by look: chips named after engines and families ("Shaders", "Mycelium", "Truchet") told a
 * music lover nothing, and "Shaders" held sixty unrelated looks. People browse visualizers by
 * picture and feeling (docs/ui-redesign; the owner chose these groups on 2026-11-02). The engine's
 * kinds and families stay searchable through [StyleEntry.kindLabel] and [StyleEntry.familyLabel].
 *
 * A plain enum on purpose: which group a style lands in is decided in `ui/StylesPage`
 * (`StyleLookTags`), which reads each style's family and kind from the native catalogue
 * (`render/scene/Catalogue`, ADR 0003) instead of guessing them from id prefixes here. This
 * package stays pure and engine-free for `:core:model`. The names people read are string resources
 * (`styles_look_*`), mapped in the same file, so this enum carries no English.
 */
enum class StyleLook {
    FLOWING,
    LIVING,
    PSYCHEDELIC,
    FRACTALS,
    SPACE,
    FLY_THROUGH,
    GLASS,
    PATTERNS,
    CLASSIC,
}
