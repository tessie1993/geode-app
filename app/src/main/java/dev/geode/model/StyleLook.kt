package dev.geode.model

/**
 * The Styles page's groups: what a style looks like, not which engine draws it.
 *
 * WHY by look: chips named after engines and families ("Shaders", "Mycelium", "Truchet") told a
 * music lover nothing, and "Shaders" held sixty unrelated looks. People browse visualizers by
 * picture and feeling (docs/ui-redesign; the owner chose these groups on 2026-11-02). The Styles
 * page has since gone back to families (2026-11-06, then one flat family dropdown on 2026-11-11), so
 * nothing draws these groups today; the native look column and this enum are kept in step by tests.
 *
 * A plain enum on purpose: which group a style lands in is engine data, the look column of the
 * native catalogue (core/viz/Catalogue, ADR 0003; its `Look` names match these entries, which
 * catalogue_test and `StyleLookTest` hold), read through `render/scene/Catalogue`.
 * This package stays pure and engine-free for `:core:model`. The names people read are string resources
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
