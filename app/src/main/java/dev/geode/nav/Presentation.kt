package dev.geode.nav

/**
 * How the shell presents a destination. The design system decides what each looks like and how
 * it moves; the navigation layer only promises the ordering and what back does.
 */
enum class Presentation {
    /** The bottom of a section's stack. Sits under the section switcher; back leaves the section. */
    ROOT,

    /** Pushed over whatever is below it in the section's stack. Back pops it. */
    PAGE,

    /** Rises over the page below without replacing it. Back, or a drag down, dismisses it. */
    SHEET,
}
