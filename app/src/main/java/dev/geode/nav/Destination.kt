package dev.geode.nav

/**
 * Every place inside a section a person can be. Each destination knows its section — null for
 * the shared pages, which push onto whichever section is current — and how it is presented.
 *
 * Nothing here draws. A destination is a name and its arguments; the design system maps each
 * one to a screen.
 */
sealed interface Destination {
    val section: Section?
    val presentation: Presentation

    sealed interface Player : Destination {
        override val section: Section get() = Section.PLAYER

        /** Artwork, transport and the current track. */
        data object NowPlaying : Player {
            override val presentation: Presentation get() = Presentation.ROOT
        }

        /** The editable up-next list. */
        data object Queue : Player {
            override val presentation: Presentation get() = Presentation.SHEET
        }

        /** Timed lyrics for the current track. */
        data object Lyrics : Player {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object SleepTimer : Player {
            override val presentation: Presentation get() = Presentation.SHEET
        }
    }

    sealed interface Library : Destination {
        override val section: Section get() = Section.LIBRARY

        /** One [view] of the whole library; changing the view replaces the root in place. */
        data class Browse(
            val view: LibraryView = LibraryView.TRACKS,
        ) : Library {
            override val presentation: Presentation get() = Presentation.ROOT
        }

        data class Album(
            val name: String,
        ) : Library {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data class Artist(
            val name: String,
        ) : Library {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        /** A MediaStore folder or a SAF root, by its path. */
        data class Folder(
            val path: String,
        ) : Library {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data class Playlist(
            val id: String,
        ) : Library {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        /** The smart-playlist rule editor; a null [id] starts a new one. */
        data class SmartPlaylist(
            val id: String? = null,
        ) : Library {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object Duplicates : Library {
            override val presentation: Presentation get() = Presentation.PAGE
        }
    }

    sealed interface Visuals : Destination {
        override val section: Section get() = Section.VISUALS

        /** The scene gallery. */
        data object Hub : Visuals {
            override val presentation: Presentation get() = Presentation.ROOT
        }

        /** Every parameter of the current scene, with locks and the randomizer. */
        data object Customize : Visuals {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object Presets : Visuals {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        /** LFO and ADSR modulation over parameters. */
        data object Modulation : Visuals {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object Palette : Visuals {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object ShaderEditor : Visuals {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object Background : Visuals {
            override val presentation: Presentation get() = Presentation.SHEET
        }

        data object Layers : Visuals {
            override val presentation: Presentation get() = Presentation.SHEET
        }
    }

    sealed interface Studio : Destination {
        override val section: Section get() = Section.STUDIO

        data object Projects : Studio {
            override val presentation: Presentation get() = Presentation.ROOT
        }

        /** The multi-lane timeline for one project. */
        data class Editor(
            val projectId: String,
        ) : Studio {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object Templates : Studio {
            override val presentation: Presentation get() = Presentation.SHEET
        }

        data object LoopRender : Studio {
            override val presentation: Presentation get() = Presentation.SHEET
        }
    }

    sealed interface Settings : Destination {
        override val section: Section get() = Section.SETTINGS

        data object Root : Settings {
            override val presentation: Presentation get() = Presentation.ROOT
        }

        data object Playback : Settings {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        /** Equalizer, ReplayGain, the native engine, bit-perfect output. */
        data object Audio : Settings {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object Look : Settings {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object Behavior : Settings {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        /** Library folder roots and SAF imports. */
        data object Folders : Settings {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        /** Other apps' audio and the microphone. */
        data object ExternalAudio : Settings {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object AutoVisuals : Settings {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object Export : Settings {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object Help : Settings {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data object About : Settings {
            override val presentation: Presentation get() = Presentation.PAGE
        }
    }

    /** Pages any section can push; they stay in the section that opened them. */
    sealed interface Shared : Destination {
        override val section: Section? get() = null

        /** Tag view and editor for one track. */
        data class TrackInfo(
            val uri: String,
        ) : Shared {
            override val presentation: Presentation get() = Presentation.PAGE
        }

        data class AddToPlaylist(
            val uri: String,
        ) : Shared {
            override val presentation: Presentation get() = Presentation.SHEET
        }
    }
}

/** The ways the library root can be listed. */
enum class LibraryView {
    TRACKS,
    ALBUMS,
    ARTISTS,
    FOLDERS,
    PLAYLISTS,
    FAVOURITES,
    RECENT,
}
