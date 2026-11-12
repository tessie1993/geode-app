package dev.geode.model

/**
 * The tab Library opens on (Settings › Preferences › Library). [TRACKS] is the tab it always
 * opened on before the choice existed. Duplicates is not offered: it is a clean-up page, not a way
 * into the music.
 */
enum class LibraryStartTab {
    TRACKS,
    ALBUMS,
    ARTISTS,
    FOLDERS,
    PLAYLISTS,
    ;

    companion object {
        fun fromName(name: String?): LibraryStartTab = entries.firstOrNull { it.name == name } ?: TRACKS
    }
}
