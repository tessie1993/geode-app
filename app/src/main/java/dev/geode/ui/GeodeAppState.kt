package dev.geode.ui

import android.view.Display
import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import dev.geode.R
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.StudioPrefsStore
import dev.geode.data.UserIntent
import dev.geode.ui.theme.StoneIcon

/**
 * The app's top-level screens, in the order the navigation bar shows them.
 *
 * Each destination carries its own label and icon so the bar is built from this list rather
 * than from a parallel one — a destination cannot exist without a way to reach it, and a
 * `when` over these has no `else`, so adding a screen is a compile error until it is handled.
 */
enum class GeodeDestination(
    @param:StringRes val labelRes: Int,
    val icon: StoneIcon,
) {
    PLAYER(R.string.nav_player, StoneIcon.PLAY),
    LIBRARY(R.string.nav_library, StoneIcon.LIBRARY),
    VISUALS(R.string.nav_visuals, StoneIcon.VISUALIZER),
    STUDIO(R.string.nav_studio, StoneIcon.STUDIO),
    SETTINGS(R.string.nav_settings, StoneIcon.SETTINGS),
}

/**
 * The tabs of [GeodeDestination.STUDIO], in the order they show. Studio owns everything that makes
 * a video: [RENDER] makes a video of what is playing (open to free and trial users within
 * `pro/FreeTier`), [MY_VIDEOS] lists every finished render and recorded take, and [EDIT] is the Pro
 * editor's home (new project, recent, all projects) in front of the timeline. Render comes first
 * because it is where a first visit and every free user lands; the last tab used is kept across
 * restarts by `data/StudioPrefsStore`.
 */
enum class StudioSection { RENDER, MY_VIDEOS, EDIT }

@Stable
class GeodeAppState(
    dest: GeodeDestination = GeodeDestination.PLAYER,
    expanded: Boolean = false,
    searching: Boolean = false,
    bootDone: Boolean = false,
    studioSection: StudioSection = StudioSection.RENDER,
) {
    var dest by mutableStateOf(dest)

    var expanded by mutableStateOf(expanded)

    var searching by mutableStateOf(searching)

    var bootDone by mutableStateOf(bootDone)

    var studioSection by mutableStateOf(studioSection)

    /**
     * Set by a screen that needs the whole window (Studio's full-screen timeline): the floating
     * mini player and tab bar step aside while it is true. Not saved: the screen sets it again.
     */
    var immersive by mutableStateOf(false)

    var externalDisplay: Display? = null
        internal set

    val onPlayer: Boolean get() = dest == GeodeDestination.PLAYER

    fun openSearch() {
        searching = true
    }

    fun closeSearch() {
        searching = false
    }

    fun expand() {
        expanded = true
    }

    fun collapse() {
        expanded = false
    }

    fun navigateTo(destination: GeodeDestination) {
        dest = destination
    }

    /** Opens Studio › Render: the one place export, loop render and their defaults live. */
    fun openStudioRender() {
        studioSection = StudioSection.RENDER
        dest = GeodeDestination.STUDIO
    }

    fun resetToPlayer() {
        dest = GeodeDestination.PLAYER
    }

    companion object {
        val Saver: Saver<GeodeAppState, List<Any>> =
            Saver(
                // Saved as the enum name, not its ordinal: reordering the navigation bar then
                // cannot silently restore a process-death survivor onto a different screen.
                save = { listOf(it.dest.name, it.expanded, it.searching, it.bootDone, it.studioSection.name) },
                restore = {
                    GeodeAppState(
                        dest =
                            GeodeDestination.entries.firstOrNull { d -> d.name == it[0] }
                                ?: GeodeDestination.PLAYER,
                        expanded = it[1] as Boolean,
                        searching = it[2] as Boolean,
                        bootDone = it[3] as Boolean,
                        studioSection =
                            StudioSection.entries.firstOrNull { s -> s.name == it.getOrNull(4) }
                                ?: StudioSection.RENDER,
                    )
                },
            )
    }
}

@Composable
fun rememberGeodeAppState(externalDisplay: Display?): GeodeAppState {
    val context = LocalContext.current
    val state =
        rememberSaveable(saver = GeodeAppState.Saver) {
            // A cold start opens Studio on the tab the user last left it on; a process-death
            // restore keeps whatever the saver brought back instead.
            val lastTab = StudioPrefsStore(GeodePrefsFiles(context).general).lastTab()
            GeodeAppState(studioSection = StudioSection.entries.firstOrNull { it.name == lastTab } ?: StudioSection.RENDER)
        }
    state.externalDisplay = externalDisplay
    return state
}

/** Where the app opens. Video-first users land in Studio, everyone else on the Stage. */
val UserIntent.landingDestination: GeodeDestination
    get() =
        when (this) {
            UserIntent.LISTENING, UserIntent.BOTH -> GeodeDestination.PLAYER
            UserIntent.MAKING_VIDEOS -> GeodeDestination.STUDIO
        }
