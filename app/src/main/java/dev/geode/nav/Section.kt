package dev.geode.nav

/**
 * The top-level sections, in the order a section switcher lists them.
 *
 * A destination belongs to exactly one section — or to none, for the shared pages any section
 * can push — so a `when` over these has no `else`: adding a section is a compile error until
 * every switch handles it.
 */
enum class Section {
    PLAYER,
    LIBRARY,
    VISUALS,
    STUDIO,
    SETTINGS,
    ;

    /** What this section shows with nothing pushed over it. */
    val root: Destination
        get() =
            when (this) {
                PLAYER -> Destination.Player.NowPlaying
                LIBRARY -> Destination.Library.Browse()
                VISUALS -> Destination.Visuals.Hub
                STUDIO -> Destination.Studio.Projects
                SETTINGS -> Destination.Settings.Root
            }
}
