package dev.geode.nav

import java.net.URLDecoder
import java.net.URLEncoder

/**
 * The string form of every [Destination] and [Overlay]: `section/page/arg…`, arguments
 * percent-encoded so a name with a slash in it stays one segment. Used by [NavSaver], and
 * readable enough to log. Decoding is total: an unknown or malformed route is null, never an
 * exception.
 */
object Routes {
    fun encode(destination: Destination): String =
        when (destination) {
            Destination.Player.NowPlaying -> "player"
            Destination.Player.Queue -> "player/queue"
            Destination.Player.Lyrics -> "player/lyrics"
            Destination.Player.SleepTimer -> "player/sleep"
            is Destination.Library.Browse -> "library/${destination.view.name}"
            is Destination.Library.Album -> "library/album/${arg(destination.name)}"
            is Destination.Library.Artist -> "library/artist/${arg(destination.name)}"
            is Destination.Library.Folder -> "library/folder/${arg(destination.path)}"
            is Destination.Library.Playlist -> "library/playlist/${arg(destination.id)}"
            is Destination.Library.SmartPlaylist ->
                destination.id?.let { "library/smart/${arg(it)}" } ?: "library/smart"
            Destination.Library.Duplicates -> "library/duplicates"
            Destination.Visuals.Hub -> "visuals"
            Destination.Visuals.Customize -> "visuals/customize"
            Destination.Visuals.Presets -> "visuals/presets"
            Destination.Visuals.Modulation -> "visuals/modulation"
            Destination.Visuals.Palette -> "visuals/palette"
            Destination.Visuals.ShaderEditor -> "visuals/shader"
            Destination.Visuals.Background -> "visuals/background"
            Destination.Visuals.Layers -> "visuals/layers"
            Destination.Studio.Projects -> "studio"
            is Destination.Studio.Editor -> "studio/editor/${arg(destination.projectId)}"
            Destination.Studio.Templates -> "studio/templates"
            Destination.Studio.LoopRender -> "studio/loop"
            Destination.Settings.Root -> "settings"
            Destination.Settings.Playback -> "settings/playback"
            Destination.Settings.Audio -> "settings/audio"
            Destination.Settings.Look -> "settings/look"
            Destination.Settings.Behavior -> "settings/behavior"
            Destination.Settings.Folders -> "settings/folders"
            Destination.Settings.ExternalAudio -> "settings/external"
            Destination.Settings.AutoVisuals -> "settings/auto-visuals"
            Destination.Settings.Export -> "settings/export"
            Destination.Settings.Help -> "settings/help"
            Destination.Settings.About -> "settings/about"
            is Destination.Shared.TrackInfo -> "track/${arg(destination.uri)}"
            is Destination.Shared.AddToPlaylist -> "track/${arg(destination.uri)}/add"
        }

    fun decode(route: String): Destination? {
        val parts = route.split('/')
        val second = parts.getOrNull(1)
        val arg = { index: Int -> parts.getOrNull(index)?.let(::unarg) }
        return when (parts[0]) {
            "player" ->
                when (second) {
                    null -> Destination.Player.NowPlaying
                    "queue" -> Destination.Player.Queue
                    "lyrics" -> Destination.Player.Lyrics
                    "sleep" -> Destination.Player.SleepTimer
                    else -> null
                }
            "library" ->
                when (second) {
                    null -> Destination.Library.Browse()
                    "album" -> arg(2)?.let { Destination.Library.Album(it) }
                    "artist" -> arg(2)?.let { Destination.Library.Artist(it) }
                    "folder" -> arg(2)?.let { Destination.Library.Folder(it) }
                    "playlist" -> arg(2)?.let { Destination.Library.Playlist(it) }
                    "smart" -> Destination.Library.SmartPlaylist(arg(2))
                    "duplicates" -> Destination.Library.Duplicates
                    else -> LibraryView.entries.firstOrNull { it.name == second }?.let { Destination.Library.Browse(it) }
                }
            "visuals" ->
                when (second) {
                    null -> Destination.Visuals.Hub
                    "customize" -> Destination.Visuals.Customize
                    "presets" -> Destination.Visuals.Presets
                    "modulation" -> Destination.Visuals.Modulation
                    "palette" -> Destination.Visuals.Palette
                    "shader" -> Destination.Visuals.ShaderEditor
                    "background" -> Destination.Visuals.Background
                    "layers" -> Destination.Visuals.Layers
                    else -> null
                }
            "studio" ->
                when (second) {
                    null -> Destination.Studio.Projects
                    "editor" -> arg(2)?.let { Destination.Studio.Editor(it) }
                    "templates" -> Destination.Studio.Templates
                    "loop" -> Destination.Studio.LoopRender
                    else -> null
                }
            "settings" ->
                when (second) {
                    null -> Destination.Settings.Root
                    "playback" -> Destination.Settings.Playback
                    "audio" -> Destination.Settings.Audio
                    "look" -> Destination.Settings.Look
                    "behavior" -> Destination.Settings.Behavior
                    "folders" -> Destination.Settings.Folders
                    "external" -> Destination.Settings.ExternalAudio
                    "auto-visuals" -> Destination.Settings.AutoVisuals
                    "export" -> Destination.Settings.Export
                    "help" -> Destination.Settings.Help
                    "about" -> Destination.Settings.About
                    else -> null
                }
            "track" ->
                arg(1)?.let { uri ->
                    if (parts.getOrNull(2) == "add") Destination.Shared.AddToPlaylist(uri) else Destination.Shared.TrackInfo(uri)
                }
            else -> null
        }
    }

    fun encode(overlay: Overlay): String =
        when (overlay) {
            Overlay.Search -> "search"
            Overlay.Visualizer -> "visualizer"
            Overlay.Export -> "export"
        }

    fun decodeOverlay(route: String): Overlay? =
        when (route) {
            "search" -> Overlay.Search
            "visualizer" -> Overlay.Visualizer
            "export" -> Overlay.Export
            else -> null
        }

    // The charset-name overloads: the Charset ones arrived in API 33.
    private fun arg(value: String): String = URLEncoder.encode(value, "UTF-8")

    private fun unarg(value: String): String? = runCatching { URLDecoder.decode(value, "UTF-8") }.getOrNull()
}
