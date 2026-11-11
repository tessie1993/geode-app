package dev.geode.ui

import android.view.Display
import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import dev.geode.R
import dev.geode.data.BehaviourPrefs
import dev.geode.data.BehaviourStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.StartScreen
import dev.geode.data.StudioPrefsStore
import dev.geode.ui.theme.StoneIcon
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

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
 * `pro/FreeTier`) and [MY_VIDEOS] lists every finished render and recorded take. Render comes first
 * because it is where a first visit and every free user lands; the last tab used is kept across
 * restarts by `data/StudioPrefsStore`. There was a third tab, Edit, until the owner dropped video
 * editing (#227): [fromName] reads a name saved back then, or any unknown one, as Render.
 */
enum class StudioSection {
    RENDER,
    MY_VIDEOS,
    ;

    companion object {
        /** The tab saved as [name], or null for none; the removed "EDIT" reads as [RENDER]. */
        fun fromName(name: String?): StudioSection? =
            when (name) {
                null -> null
                REMOVED_EDIT -> RENDER
                else -> entries.firstOrNull { it.name == name }
            }

        private const val REMOVED_EDIT = "EDIT"
    }
}

@Stable
class GeodeAppState(
    dest: GeodeDestination = GeodeDestination.PLAYER,
    searching: Boolean = false,
    bootDone: Boolean = false,
    studioSection: StudioSection = StudioSection.RENDER,
) {
    var dest by mutableStateOf(dest)

    var searching by mutableStateOf(searching)

    var bootDone by mutableStateOf(bootDone)

    var studioSection by mutableStateOf(studioSection)

    var externalDisplay: Display? = null
        internal set

    val onPlayer: Boolean get() = dest == GeodeDestination.PLAYER

    fun openSearch() {
        searching = true
    }

    fun closeSearch() {
        searching = false
    }

    /** Every change of destination goes through here, and closes the search overlay over it. */
    fun navigateTo(destination: GeodeDestination) {
        searching = false
        dest = destination
    }

    /**
     * A Settings group another screen asked for, opened once by `AppSettingsTab` and then cleared.
     * Not saved: a process-death restore lands on Settings' own remembered group.
     */
    var settingsGroupRequest by mutableStateOf<SettingsGroupKey?>(null)

    /**
     * A page of the Visuals options another screen asked for (the tour's replay), opened once by
     * `VisualsHub` and then cleared. Not saved, like [settingsGroupRequest].
     */
    internal var visualsPageRequest by mutableStateOf<VisualsPage?>(null)

    /** Opens Visuals with its options up on [page]. */
    internal fun openVisualsPage(page: VisualsPage) {
        visualsPageRequest = page
        navigateTo(GeodeDestination.VISUALS)
    }

    /** Opens Settings › Library: where music folders and the short-track filter live. */
    fun openSettingsFolders() {
        settingsGroupRequest = SettingsGroupKey.LIBRARY
        navigateTo(GeodeDestination.SETTINGS)
    }

    /** Opens Studio › Render: the one place export, loop render and their defaults live. */
    fun openStudioRender() {
        studioSection = StudioSection.RENDER
        navigateTo(GeodeDestination.STUDIO)
    }

    fun resetToPlayer() = navigateTo(GeodeDestination.PLAYER)

    companion object {
        val Saver: Saver<GeodeAppState, List<Any>> =
            Saver(
                // Saved as the enum name, not its ordinal: reordering the navigation bar then
                // cannot silently restore a process-death survivor onto a different screen.
                save = { listOf(it.dest.name, it.searching, it.bootDone, it.studioSection.name) },
                restore = {
                    GeodeAppState(
                        dest =
                            GeodeDestination.entries.firstOrNull { d -> d.name == it[0] }
                                ?: GeodeDestination.PLAYER,
                        searching = it[1] as Boolean,
                        bootDone = it[2] as Boolean,
                        studioSection = StudioSection.fromName(it.getOrNull(3) as? String) ?: StudioSection.RENDER,
                    )
                },
            )
    }
}

@Composable
fun rememberGeodeAppState(externalDisplay: Display?): GeodeAppState {
    val context = LocalContext.current
    val general = remember(context) { GeodePrefsFiles(context).general }
    val state =
        rememberSaveable(saver = GeodeAppState.Saver) {
            // A cold start opens on Settings › Behaviour's start screen, and Studio on the tab the
            // user last left it on; a process-death restore keeps whatever the saver brought back
            // instead. Both read the general prefs file, already parsed by the Activity's ViewModels.
            val lastTab = StudioPrefsStore(general).lastTab()
            GeodeAppState(
                dest = BehaviourStore(general).load().startDestination(),
                studioSection = StudioSection.fromName(lastTab) ?: StudioSection.RENDER,
            )
        }
    state.externalDisplay = externalDisplay
    // "Where you left off" reopens the last screen shown. Recorded on IO each time it changes;
    // Settings is never a start screen, so leaving the app there reopens the screen before it.
    LaunchedEffect(state.dest) {
        val screen = state.dest.startScreen ?: return@LaunchedEffect
        withContext(Dispatchers.IO) { BehaviourStore(general).saveLastScreen(screen) }
    }
    return state
}

/**
 * The screen a cold start opens on: Settings › Behaviour's "Open on" (#337), with "Where you left
 * off" resolved to the last screen recorded. Pure, so the rule is tested without a device.
 */
internal fun BehaviourPrefs.startDestination(): GeodeDestination =
    when (opensOn) {
        // opensOn never returns LAST_USED (it resolves to lastScreen); Player is the safe floor.
        StartScreen.LAST_USED, StartScreen.PLAYER -> GeodeDestination.PLAYER
        StartScreen.LIBRARY -> GeodeDestination.LIBRARY
        StartScreen.VISUALS -> GeodeDestination.VISUALS
        StartScreen.STUDIO -> GeodeDestination.STUDIO
    }

/** This destination as a start screen; null for Settings, which the app never opens on. */
internal val GeodeDestination.startScreen: StartScreen?
    get() =
        when (this) {
            GeodeDestination.PLAYER -> StartScreen.PLAYER
            GeodeDestination.LIBRARY -> StartScreen.LIBRARY
            GeodeDestination.VISUALS -> StartScreen.VISUALS
            GeodeDestination.STUDIO -> StartScreen.STUDIO
            GeodeDestination.SETTINGS -> null
        }
