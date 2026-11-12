package dev.geode.playback

import android.content.Context
import android.content.SharedPreferences
import android.os.Bundle
import androidx.annotation.OptIn
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.CommandButton
import androidx.media3.session.MediaSession
import androidx.media3.session.SessionCommand
import androidx.media3.session.SessionCommands
import androidx.media3.session.SessionResult
import dev.geode.R
import dev.geode.data.FavouritesStore
import dev.geode.data.NotificationButton
import dev.geode.geodeContainer
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

/**
 * Settings › Output & devices › Notification buttons: the extra buttons the media notification (and the
 * lock screen, Android Auto and the system media controls, which share it) carries beside previous,
 * play and next. The default is shuffle and repeat. Favourite can be added.
 *
 * Each button is a custom session command that toggles its state, so its icon always shows the state
 * now. [refresh] runs after every change that could alter an icon, and whenever the choice in
 * Settings changes (it follows the container's one copy, so it also lands once a cold start's load does).
 */
@OptIn(UnstableApi::class)
internal class NotificationButtons(
    private val context: Context,
    private val player: Player,
    private val scope: CoroutineScope,
) : Player.Listener {
    private val prefsFiles = context.geodeContainer.prefsFiles
    private val behaviour = context.geodeContainer.behaviour

    private var session: MediaSession? = null

    /** The heart follows a favourite changed in the app too. A field: listeners are held weakly. */
    private val favouritesChanged = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> refresh() }

    private var choiceChanged: Job? = null

    fun attach(session: MediaSession) {
        this.session = session
        prefsFiles.favourites.registerOnSharedPreferenceChangeListener(favouritesChanged)
        choiceChanged =
            scope.launch {
                behaviour.value
                    .map { it.notificationButtons }
                    .distinctUntilChanged()
                    .collect { refresh() }
            }
    }

    fun detach() {
        prefsFiles.favourites.unregisterOnSharedPreferenceChangeListener(favouritesChanged)
        choiceChanged?.cancel()
        choiceChanged = null
        session = null
    }

    /** Added to every controller's session commands, so the buttons are allowed to send them. */
    fun commands(base: SessionCommands): SessionCommands = base.buildUpon().addSessionCommands(ALL).build()

    fun buttons(): List<CommandButton> =
        NotificationButton.ordered(behaviour.value.value.notificationButtons).mapNotNull { button ->
            when (button) {
                NotificationButton.SHUFFLE -> shuffleButton()
                NotificationButton.REPEAT -> repeatButton()
                NotificationButton.FAVOURITE -> favouriteButton()
            }
        }

    /** Handles one of [ALL]; anything else is not ours. */
    fun onCustomCommand(command: SessionCommand): SessionResult? {
        when (command.customAction) {
            SHUFFLE -> player.shuffleModeEnabled = !player.shuffleModeEnabled
            REPEAT ->
                player.repeatMode =
                    when (player.repeatMode) {
                        Player.REPEAT_MODE_OFF -> Player.REPEAT_MODE_ALL
                        Player.REPEAT_MODE_ALL -> Player.REPEAT_MODE_ONE
                        else -> Player.REPEAT_MODE_OFF
                    }
            FAVOURITE -> currentUri()?.let { FavouritesStore(prefsFiles.favourites).toggle(it) }
            else -> return null
        }
        refresh()
        return SessionResult(SessionResult.RESULT_SUCCESS)
    }

    fun refresh() {
        session?.setMediaButtonPreferences(buttons())
    }

    override fun onShuffleModeEnabledChanged(shuffleModeEnabled: Boolean) = refresh()

    override fun onRepeatModeChanged(repeatMode: Int) = refresh()

    override fun onMediaItemTransition(
        mediaItem: MediaItem?,
        reason: Int,
    ) = refresh()

    private fun shuffleButton(): CommandButton {
        val on = player.shuffleModeEnabled
        return button(
            if (on) CommandButton.ICON_SHUFFLE_ON else CommandButton.ICON_SHUFFLE_OFF,
            if (on) R.string.notification_shuffle_off else R.string.notification_shuffle_on,
            SHUFFLE,
        )
    }

    private fun repeatButton(): CommandButton =
        when (player.repeatMode) {
            Player.REPEAT_MODE_ALL -> button(CommandButton.ICON_REPEAT_ALL, R.string.notification_repeat_one, REPEAT)
            Player.REPEAT_MODE_ONE -> button(CommandButton.ICON_REPEAT_ONE, R.string.notification_repeat_off, REPEAT)
            else -> button(CommandButton.ICON_REPEAT_OFF, R.string.notification_repeat_all, REPEAT)
        }

    private fun favouriteButton(): CommandButton? {
        val uri = currentUri() ?: return null
        val liked = FavouritesStore(prefsFiles.favourites).isFavourite(uri)
        return button(
            if (liked) CommandButton.ICON_HEART_FILLED else CommandButton.ICON_HEART_UNFILLED,
            if (liked) R.string.notification_unfavourite else R.string.notification_favourite,
            FAVOURITE,
        )
    }

    private fun button(
        icon: Int,
        label: Int,
        action: String,
    ): CommandButton =
        CommandButton
            .Builder(icon)
            .setDisplayName(context.getString(label))
            .setSessionCommand(SessionCommand(action, Bundle.EMPTY))
            .build()

    private fun currentUri(): String? =
        player.currentMediaItem
            ?.localConfiguration
            ?.uri
            ?.toString()

    private companion object {
        const val SHUFFLE = "dev.geode.notification.SHUFFLE"
        const val REPEAT = "dev.geode.notification.REPEAT"
        const val FAVOURITE = "dev.geode.notification.FAVOURITE"
        val ALL: List<SessionCommand> = listOf(SHUFFLE, REPEAT, FAVOURITE).map { SessionCommand(it, Bundle.EMPTY) }
    }
}
