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
import dev.geode.R
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

@Stable
class GeodeAppState(
    dest: GeodeDestination = GeodeDestination.PLAYER,
    expanded: Boolean = false,
    searching: Boolean = false,
    bootDone: Boolean = false,
) {
    var dest by mutableStateOf(dest)

    var expanded by mutableStateOf(expanded)

    var searching by mutableStateOf(searching)

    var bootDone by mutableStateOf(bootDone)

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

    fun resetToPlayer() {
        dest = GeodeDestination.PLAYER
    }

    companion object {
        val Saver: Saver<GeodeAppState, List<Any>> =
            Saver(
                // Saved as the enum name, not its ordinal: reordering the navigation bar then
                // cannot silently restore a process-death survivor onto a different screen.
                save = { listOf(it.dest.name, it.expanded, it.searching, it.bootDone) },
                restore = {
                    GeodeAppState(
                        dest =
                            GeodeDestination.entries.firstOrNull { d -> d.name == it[0] }
                                ?: GeodeDestination.PLAYER,
                        expanded = it[1] as Boolean,
                        searching = it[2] as Boolean,
                        bootDone = it[3] as Boolean,
                    )
                },
            )
    }
}

@Composable
fun rememberGeodeAppState(externalDisplay: Display?): GeodeAppState {
    val state = rememberSaveable(saver = GeodeAppState.Saver) { GeodeAppState() }
    state.externalDisplay = externalDisplay
    return state
}
