package dev.geode.data

import android.content.SharedPreferences
import androidx.core.content.edit

// Settings › Behaviour: what the app does by itself, what each screen does, and what each gesture on
// the visual means. The owner decided every default here (wave 1, 1C); the screens that read them
// come later, so this file is the record of the decision as much as the store of the choice. Kept in
// `geode-prefs`, so Settings › Backup carries it in the Settings section.
// "Resume last track on launch" is not here: it is PlayerPrefs.autoResume, which already existed.

/** The screen the app opens on. [LAST_USED] reopens [BehaviourPrefs.lastScreen]. */
enum class StartScreen {
    LAST_USED,
    PLAYER,
    LIBRARY,
    VISUALS,
    STUDIO,
}

/** How the fullscreen visual turns with the phone. */
enum class VisualsOrientation {
    AUTO_ROTATE,
    PORTRAIT,
    LANDSCAPE,
}

/** The extra buttons the media notification carries beside play, previous and next. */
enum class NotificationButton {
    SHUFFLE,
    REPEAT,
    FAVOURITE,
}

/** What the volume keys do while Visuals is on screen. */
enum class VolumeKeysAction {
    VOLUME,
    STYLE,
}

/** What shaking the phone does. */
enum class ShakeAction {
    OFF,
    NEXT_TRACK,
    NEXT_STYLE,
}

/** What tapping a song in a list plays. */
enum class TapSongAction {
    WHOLE_LIST,
    ONLY_IT,
    ADD_TO_QUEUE,
}

/** What happens when the queue runs out. */
enum class QueueEndAction {
    STOP,
    REPEAT,
    SIMILAR,
}

/** What swiping a song row does. */
enum class SwipeRowAction {
    OFF,
    QUEUE_NEXT,
    ADD_TO_QUEUE,
}

/** What fills the Player screen's art area. */
enum class PlayerArt {
    LIVE_VISUAL,
    ARTWORK,
}

/** Whether the bottom bar shows on Visuals. */
enum class VisualsBar {
    ALWAYS,
    AUTO_HIDE,
    NEVER,
}

/**
 * A gesture on the visual (ui-flow-decisions.md › Gestures). Swipes count only when they start
 * outside the system gesture insets; the edges stay Android's.
 */
enum class CanvasGesture {
    /** A tap on the lower part of the visual. */
    TAP_BOTTOM,

    /** A tap on the upper part of the visual. */
    TAP_TOP,
    DOUBLE_TAP,
    SWIPE_SIDEWAYS,
    SWIPE_UP,
    SWIPE_DOWN,

    /** Held through the red ring (~0.8 s); lifting early does nothing. */
    LONG_PRESS,

    /** Two circles drawn with one finger. */
    SWIRL,
    TWO_FINGER_DRAG,
    PINCH_TWIST,
}

/** What a [CanvasGesture] does. Not every action suits every gesture; the settings screen offers the ones that do. */
enum class GestureAction {
    OFF,

    /** The mini player with the stage icons (Record, Fullscreen, ‹ ›, Studio). */
    SHOW_MINI_PLAYER,

    /** Visuals quick settings. */
    QUICK_SETTINGS,

    /** The options sheet: hidden, or back at its last height. */
    TOGGLE_SHEET,

    /** Next or previous by the swipe's direction: a style, a preset, or the visual playlist's look. */
    STEP_STYLE,
    STEP_PRESET,
    STEP_PLAYLIST,

    /** The Macros drawer, with its edit and make-a-macro icon. */
    MACROS,

    /** Record a take; while recording, the red icon stops it. */
    RECORD,
    RANDOMIZE_ALL,

    /** Ripples, smear and stirring the liquid. */
    TOUCH_PLAY,
    PLAY_PAUSE,
}

data class BehaviourPrefs(
    /** The app opens on the Library unless the user picks another screen (owner). */
    val startScreen: StartScreen = StartScreen.LIBRARY,
    /** Where [StartScreen.LAST_USED] reopens: the last screen shown, never [StartScreen.LAST_USED] itself. */
    val lastScreen: StartScreen = StartScreen.PLAYER,
    val visualsOrientation: VisualsOrientation = VisualsOrientation.AUTO_ROTATE,
    /** Keeps the screen awake while Visuals is on screen; Player's own keep-screen-on is apart from it. */
    val keepScreenOnVisuals: Boolean = true,
    /** Plays when headphones or a Bluetooth audio device connect, while a track is loaded and paused. */
    val startOnHeadphones: Boolean = false,
    val autoFullscreen: Boolean = false,
    /** Back twice to leave the app. */
    val confirmExit: Boolean = false,
    val notificationButtons: Set<NotificationButton> = setOf(NotificationButton.SHUFFLE, NotificationButton.REPEAT),
    val volumeKeysOnVisuals: VolumeKeysAction = VolumeKeysAction.VOLUME,
    val shake: ShakeAction = ShakeAction.OFF,
    val tapSong: TapSongAction = TapSongAction.WHOLE_LIST,
    val rememberShuffleRepeat: Boolean = true,
    val queueEnd: QueueEndAction = QueueEndAction.STOP,
    val swipeRow: SwipeRowAction = SwipeRowAction.QUEUE_NEXT,
    val playerArt: PlayerArt = PlayerArt.LIVE_VISUAL,
    /** Visuals reopens on the panel page it was left on, rather than on Styles. */
    val rememberVisualsPage: Boolean = true,
    val visualsBar: VisualsBar = VisualsBar.AUTO_HIDE,
    /** Tips on first use of each screen, until each has been seen once. */
    val showTips: Boolean = true,
    /**
     * The dream (screen saver) at full brightness. Its own switch: it used to follow Player's
     * keep-screen-on, which is about a different screen. On by default (owner): a visualizer
     * screen saver is there to be seen; the user can dim it.
     */
    val dreamScreenBright: Boolean = true,
    /** The dream's clock (time and date) over the visual; a dream is mostly a bedside clock. */
    val dreamShowClock: Boolean = true,
    /** The dream's "title · artist" line under the clock, while something plays. */
    val dreamShowNowPlaying: Boolean = true,
    /** Every canvas gesture's action. A gesture missing from the map is [GestureAction.OFF]. */
    val gestures: Map<CanvasGesture, GestureAction> = DEFAULT_GESTURES,
) {
    /** The screen to open on now. */
    val opensOn: StartScreen
        get() = if (startScreen == StartScreen.LAST_USED) lastScreen else startScreen

    fun actionFor(gesture: CanvasGesture): GestureAction = gestures[gesture] ?: GestureAction.OFF

    companion object {
        /** The owner's gesture set (ui-flow-decisions.md › Gestures); pinch and twist do nothing. */
        val DEFAULT_GESTURES: Map<CanvasGesture, GestureAction> =
            mapOf(
                CanvasGesture.TAP_BOTTOM to GestureAction.SHOW_MINI_PLAYER,
                CanvasGesture.TAP_TOP to GestureAction.QUICK_SETTINGS,
                CanvasGesture.DOUBLE_TAP to GestureAction.TOGGLE_SHEET,
                CanvasGesture.SWIPE_SIDEWAYS to GestureAction.STEP_STYLE,
                CanvasGesture.SWIPE_UP to GestureAction.SHOW_MINI_PLAYER,
                CanvasGesture.SWIPE_DOWN to GestureAction.MACROS,
                CanvasGesture.LONG_PRESS to GestureAction.RECORD,
                CanvasGesture.SWIRL to GestureAction.RANDOMIZE_ALL,
                CanvasGesture.TWO_FINGER_DRAG to GestureAction.TOUCH_PLAY,
                CanvasGesture.PINCH_TWIST to GestureAction.OFF,
            )
    }
}

class BehaviourStore(
    private val prefs: SharedPreferences,
) {
    fun load(): BehaviourPrefs {
        val d = BehaviourPrefs()
        return BehaviourPrefs(
            startScreen = enumOf(KEY_START_SCREEN, d.startScreen),
            lastScreen = enumOf(KEY_LAST_SCREEN, d.lastScreen).takeIf { it != StartScreen.LAST_USED } ?: d.lastScreen,
            visualsOrientation = enumOf(KEY_ORIENTATION, d.visualsOrientation),
            keepScreenOnVisuals = prefs.getBoolean(KEY_KEEP_SCREEN_ON_VISUALS, d.keepScreenOnVisuals),
            startOnHeadphones = prefs.getBoolean(KEY_START_ON_HEADPHONES, d.startOnHeadphones),
            autoFullscreen = prefs.getBoolean(KEY_AUTO_FULLSCREEN, d.autoFullscreen),
            confirmExit = prefs.getBoolean(KEY_CONFIRM_EXIT, d.confirmExit),
            notificationButtons =
                prefs
                    .getStringSet(KEY_NOTIFICATION_BUTTONS, null)
                    ?.mapNotNullTo(mutableSetOf()) { name -> NotificationButton.entries.firstOrNull { it.name == name } }
                    ?: d.notificationButtons,
            volumeKeysOnVisuals = enumOf(KEY_VOLUME_KEYS, d.volumeKeysOnVisuals),
            shake = enumOf(KEY_SHAKE, d.shake),
            tapSong = enumOf(KEY_TAP_SONG, d.tapSong),
            rememberShuffleRepeat = prefs.getBoolean(KEY_REMEMBER_SHUFFLE_REPEAT, d.rememberShuffleRepeat),
            queueEnd = enumOf(KEY_QUEUE_END, d.queueEnd),
            swipeRow = enumOf(KEY_SWIPE_ROW, d.swipeRow),
            playerArt = enumOf(KEY_PLAYER_ART, d.playerArt),
            rememberVisualsPage = prefs.getBoolean(KEY_REMEMBER_VISUALS_PAGE, d.rememberVisualsPage),
            visualsBar = enumOf(KEY_VISUALS_BAR, d.visualsBar),
            showTips = prefs.getBoolean(KEY_SHOW_TIPS, d.showTips),
            dreamScreenBright = prefs.getBoolean(KEY_DREAM_BRIGHT, d.dreamScreenBright),
            dreamShowClock = prefs.getBoolean(KEY_DREAM_CLOCK, d.dreamShowClock),
            dreamShowNowPlaying = prefs.getBoolean(KEY_DREAM_NOW_PLAYING, d.dreamShowNowPlaying),
            gestures =
                CanvasGesture.entries.associateWith { gesture ->
                    enumOf(KEY_GESTURE_PREFIX + gesture.name, d.actionFor(gesture))
                },
        )
    }

    fun save(b: BehaviourPrefs) {
        prefs.edit {
            putString(KEY_START_SCREEN, b.startScreen.name)
            putString(KEY_LAST_SCREEN, b.lastScreen.name)
            putString(KEY_ORIENTATION, b.visualsOrientation.name)
            putBoolean(KEY_KEEP_SCREEN_ON_VISUALS, b.keepScreenOnVisuals)
            putBoolean(KEY_START_ON_HEADPHONES, b.startOnHeadphones)
            putBoolean(KEY_AUTO_FULLSCREEN, b.autoFullscreen)
            putBoolean(KEY_CONFIRM_EXIT, b.confirmExit)
            putStringSet(KEY_NOTIFICATION_BUTTONS, b.notificationButtons.mapTo(mutableSetOf()) { it.name })
            putString(KEY_VOLUME_KEYS, b.volumeKeysOnVisuals.name)
            putString(KEY_SHAKE, b.shake.name)
            putString(KEY_TAP_SONG, b.tapSong.name)
            putBoolean(KEY_REMEMBER_SHUFFLE_REPEAT, b.rememberShuffleRepeat)
            putString(KEY_QUEUE_END, b.queueEnd.name)
            putString(KEY_SWIPE_ROW, b.swipeRow.name)
            putString(KEY_PLAYER_ART, b.playerArt.name)
            putBoolean(KEY_REMEMBER_VISUALS_PAGE, b.rememberVisualsPage)
            putString(KEY_VISUALS_BAR, b.visualsBar.name)
            putBoolean(KEY_SHOW_TIPS, b.showTips)
            putBoolean(KEY_DREAM_BRIGHT, b.dreamScreenBright)
            putBoolean(KEY_DREAM_CLOCK, b.dreamShowClock)
            putBoolean(KEY_DREAM_NOW_PLAYING, b.dreamShowNowPlaying)
            for (gesture in CanvasGesture.entries) putString(KEY_GESTURE_PREFIX + gesture.name, b.actionFor(gesture).name)
        }
    }

    /** Records the screen now shown, for [StartScreen.LAST_USED]; alone, so it never rewrites the rest. */
    fun saveLastScreen(screen: StartScreen) {
        if (screen == StartScreen.LAST_USED) return
        prefs.edit { putString(KEY_LAST_SCREEN, screen.name) }
    }

    /** A stored enum by name; an unknown or missing name (renamed in a later build) reads as [default]. */
    private inline fun <reified E : Enum<E>> enumOf(
        key: String,
        default: E,
    ): E = prefs.getString(key, null)?.let { name -> enumValues<E>().firstOrNull { it.name == name } } ?: default

    private companion object {
        const val KEY_START_SCREEN = "behaviour_start_screen"
        const val KEY_LAST_SCREEN = "behaviour_last_screen"
        const val KEY_ORIENTATION = "behaviour_visuals_orientation"
        const val KEY_KEEP_SCREEN_ON_VISUALS = "behaviour_keep_screen_on_visuals"
        const val KEY_START_ON_HEADPHONES = "behaviour_start_on_headphones"
        const val KEY_AUTO_FULLSCREEN = "behaviour_auto_fullscreen"
        const val KEY_CONFIRM_EXIT = "behaviour_confirm_exit"
        const val KEY_NOTIFICATION_BUTTONS = "behaviour_notification_buttons"
        const val KEY_VOLUME_KEYS = "behaviour_volume_keys_visuals"
        const val KEY_SHAKE = "behaviour_shake"
        const val KEY_TAP_SONG = "behaviour_tap_song"
        const val KEY_REMEMBER_SHUFFLE_REPEAT = "behaviour_remember_shuffle_repeat"
        const val KEY_QUEUE_END = "behaviour_queue_end"
        const val KEY_SWIPE_ROW = "behaviour_swipe_row"
        const val KEY_PLAYER_ART = "behaviour_player_art"
        const val KEY_REMEMBER_VISUALS_PAGE = "behaviour_remember_visuals_page"
        const val KEY_VISUALS_BAR = "behaviour_visuals_bar"
        const val KEY_SHOW_TIPS = "behaviour_show_tips"
        const val KEY_DREAM_BRIGHT = "behaviour_dream_screen_bright"
        const val KEY_DREAM_CLOCK = "behaviour_dream_show_clock"
        const val KEY_DREAM_NOW_PLAYING = "behaviour_dream_show_now_playing"
        const val KEY_GESTURE_PREFIX = "behaviour_gesture_"
    }
}
