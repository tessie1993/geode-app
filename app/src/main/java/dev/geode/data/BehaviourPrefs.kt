package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.BackupClass
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative

// What the app does by itself, what each screen does, and what each gesture on the visual means
// (Settings spreads these rows over seven of its tabs). The owner decided every default here (wave 1, 1C). Kept in `geode-prefs`, so
// Settings › Backup carries it in the Settings section.
// "Resume last track on launch" is not here: it is PlayerPrefs.autoResume, which already existed.

/** The screen the app opens on. [LAST_USED] reopens [BehaviourPrefs.lastScreen]. */
enum class StartScreen {
    LAST_USED,
    PLAYER,
    LIBRARY,
    VISUALS,
    STUDIO,
}

/** The extra buttons the media notification carries beside play, previous and next. */
enum class NotificationButton {
    SHUFFLE,
    REPEAT,
    FAVOURITE,
    ;

    companion object {
        /** Android's media controls draw two buttons beside previous, play and next; a third is dropped. */
        const val ROOM = 2

        /** Favourite first: it is the one asked for beyond the defaults, so it is never the one dropped. */
        private val PRIORITY = listOf(FAVOURITE, SHUFFLE, REPEAT)

        /** The order the session offers [chosen] in: controllers fill their slots from the front. */
        fun ordered(chosen: Set<NotificationButton>): List<NotificationButton> = PRIORITY.filter { it in chosen }

        /** [chosen] with [button] switched; switching one on past [ROOM] switches the last other one off. */
        fun toggled(
            chosen: Set<NotificationButton>,
            button: NotificationButton,
            on: Boolean,
        ): Set<NotificationButton> = if (on) (listOf(button) + ordered(chosen - button)).take(ROOM).toSet() else chosen - button
    }
}

/** What happens when the queue runs out. */
enum class QueueEndAction {
    STOP,
    REPEAT,
    SIMILAR,
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
    /** Keeps the screen awake while Visuals is on screen; Player's own keep-screen-on is apart from it. */
    val keepScreenOnVisuals: Boolean = true,
    /** Plays when headphones or a Bluetooth audio device connect, while a track is loaded and paused. */
    val startOnHeadphones: Boolean = false,
    val notificationButtons: Set<NotificationButton> = setOf(NotificationButton.SHUFFLE, NotificationButton.REPEAT),
    val rememberShuffleRepeat: Boolean = true,
    val queueEnd: QueueEndAction = QueueEndAction.STOP,
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
    /**
     * The dream also reacts to other apps' sound (the output-mix capture the wallpaper's "React to
     * any app" uses), when BeatRealms is not playing. Off by default, like the wallpaper's: Android
     * files that capture under the microphone permission, so it is asked for when this is turned on.
     */
    val dreamHearOtherApps: Boolean = false,
    /**
     * Behaviour › Dream's frame rate (ADR 0008), its own and not Settings › Performance's: 30 unless
     * the user picks another. The allowed rates and the snap are native (`GeodeNative.surfaceFpsChoice`).
     */
    val dreamFps: Int = GeodeNative.SURFACE_DEFAULT_FPS,
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
                CanvasGesture.TAP_TOP to GestureAction.SHOW_MINI_PLAYER,
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

/** Behaviour's options as one repository, built once by the container (#1009, PR 4). */
typealias BehaviourRepository = SettingsRepository<BehaviourPrefs>

/**
 * How [BehaviourPrefs] is kept in `geode-prefs`. An update writes only the fields it changed
 * ([writeChanges]): the last screen is recorded on every screen change, and a whole write there
 * would pin every default (the gesture map, the dream's rate) for users who never chose one.
 */
object BehaviourCodec : PrefsCodec<BehaviourPrefs> {
    override val defaults = BehaviourPrefs()

    override fun read(prefs: SharedPreferences): BehaviourPrefs {
        val d = defaults
        return BehaviourPrefs(
            startScreen = prefs.enumOf(KEY_START_SCREEN, d.startScreen),
            lastScreen = prefs.enumOf(KEY_LAST_SCREEN, d.lastScreen),
            keepScreenOnVisuals = prefs.getBoolean(KEY_KEEP_SCREEN_ON_VISUALS, d.keepScreenOnVisuals),
            startOnHeadphones = prefs.getBoolean(KEY_START_ON_HEADPHONES, d.startOnHeadphones),
            notificationButtons =
                prefs
                    .getStringSet(KEY_NOTIFICATION_BUTTONS, null)
                    ?.mapNotNullTo(mutableSetOf()) { name -> NotificationButton.entries.firstOrNull { it.name == name } }
                    ?: d.notificationButtons,
            rememberShuffleRepeat = prefs.getBoolean(KEY_REMEMBER_SHUFFLE_REPEAT, d.rememberShuffleRepeat),
            queueEnd = prefs.enumOf(KEY_QUEUE_END, d.queueEnd),
            dreamScreenBright = prefs.getBoolean(KEY_DREAM_BRIGHT, d.dreamScreenBright),
            dreamShowClock = prefs.getBoolean(KEY_DREAM_CLOCK, d.dreamShowClock),
            dreamShowNowPlaying = prefs.getBoolean(KEY_DREAM_NOW_PLAYING, d.dreamShowNowPlaying),
            dreamHearOtherApps = prefs.getBoolean(KEY_DREAM_OTHER_APPS, d.dreamHearOtherApps),
            dreamFps = prefs.getInt(KEY_DREAM_FPS, d.dreamFps),
            gestures =
                CanvasGesture.entries.associateWith { gesture ->
                    prefs.enumOf(KEY_GESTURE_PREFIX + gesture.name, d.actionFor(gesture))
                },
        )
    }

    /** [StartScreen.LAST_USED] is a choice, never a screen shown: as a last screen it reads as the default. */
    override fun normalize(value: BehaviourPrefs): BehaviourPrefs =
        if (value.lastScreen == StartScreen.LAST_USED) value.copy(lastScreen = defaults.lastScreen) else value

    override fun write(
        editor: SharedPreferences.Editor,
        value: BehaviourPrefs,
    ) = put(editor, null, value)

    override fun writeChanges(
        editor: SharedPreferences.Editor,
        previous: BehaviourPrefs,
        value: BehaviourPrefs,
    ) = put(editor, previous, value)

    /** Every field of [value], or only those that differ from [previous] when there is one. */
    private fun put(
        editor: SharedPreferences.Editor,
        previous: BehaviourPrefs?,
        value: BehaviourPrefs,
    ) {
        fun changed(field: (BehaviourPrefs) -> Any): Boolean = previous == null || field(previous) != field(value)
        if (changed { it.startScreen }) editor.putString(KEY_START_SCREEN, value.startScreen.name)
        if (changed { it.lastScreen }) editor.putString(KEY_LAST_SCREEN, value.lastScreen.name)
        if (changed { it.keepScreenOnVisuals }) editor.putBoolean(KEY_KEEP_SCREEN_ON_VISUALS, value.keepScreenOnVisuals)
        if (changed { it.startOnHeadphones }) editor.putBoolean(KEY_START_ON_HEADPHONES, value.startOnHeadphones)
        if (changed { it.notificationButtons }) {
            editor.putStringSet(KEY_NOTIFICATION_BUTTONS, value.notificationButtons.mapTo(mutableSetOf()) { it.name })
        }
        if (changed { it.rememberShuffleRepeat }) editor.putBoolean(KEY_REMEMBER_SHUFFLE_REPEAT, value.rememberShuffleRepeat)
        if (changed { it.queueEnd }) editor.putString(KEY_QUEUE_END, value.queueEnd.name)
        if (changed { it.dreamScreenBright }) editor.putBoolean(KEY_DREAM_BRIGHT, value.dreamScreenBright)
        if (changed { it.dreamShowClock }) editor.putBoolean(KEY_DREAM_CLOCK, value.dreamShowClock)
        if (changed { it.dreamShowNowPlaying }) editor.putBoolean(KEY_DREAM_NOW_PLAYING, value.dreamShowNowPlaying)
        if (changed { it.dreamHearOtherApps }) editor.putBoolean(KEY_DREAM_OTHER_APPS, value.dreamHearOtherApps)
        if (changed { it.dreamFps }) editor.putInt(KEY_DREAM_FPS, value.dreamFps)
        for (gesture in CanvasGesture.entries) {
            if (changed { it.actionFor(gesture) }) editor.putString(KEY_GESTURE_PREFIX + gesture.name, value.actionFor(gesture).name)
        }
    }

    /** A stored enum by name; an unknown or missing name (renamed in a later build) reads as [default]. */
    private inline fun <reified E : Enum<E>> SharedPreferences.enumOf(
        key: String,
        default: E,
    ): E = getString(key, null)?.let { name -> enumValues<E>().firstOrNull { it.name == name } } ?: default

    internal const val KEY_START_SCREEN = "behaviour_start_screen"
    internal const val KEY_LAST_SCREEN = "behaviour_last_screen"
    internal const val KEY_KEEP_SCREEN_ON_VISUALS = "behaviour_keep_screen_on_visuals"
    internal const val KEY_START_ON_HEADPHONES = "behaviour_start_on_headphones"
    internal const val KEY_NOTIFICATION_BUTTONS = "behaviour_notification_buttons"
    internal const val KEY_REMEMBER_SHUFFLE_REPEAT = "behaviour_remember_shuffle_repeat"
    internal const val KEY_QUEUE_END = "behaviour_queue_end"
    internal const val KEY_DREAM_BRIGHT = "behaviour_dream_screen_bright"
    internal const val KEY_DREAM_CLOCK = "behaviour_dream_show_clock"
    internal const val KEY_DREAM_NOW_PLAYING = "behaviour_dream_show_now_playing"
    internal const val KEY_DREAM_OTHER_APPS = "behaviour_dream_hear_other_apps"
    internal const val KEY_DREAM_FPS = "behaviour_dream_fps"
    internal const val KEY_GESTURE_PREFIX = "behaviour_gesture_"

    override val keys =
        prefKeys(GeodePrefsFiles.GENERAL) {
            portable(PrefType.STRING, KEY_START_SCREEN, KEY_LAST_SCREEN, KEY_QUEUE_END)
            add(PrefType.STRING, BackupClass.PORTABLE, CanvasGesture.entries.map { KEY_GESTURE_PREFIX + it.name })
            portable(PrefType.BOOLEAN, KEY_KEEP_SCREEN_ON_VISUALS, KEY_START_ON_HEADPHONES, KEY_REMEMBER_SHUFFLE_REPEAT)
            portable(PrefType.BOOLEAN, KEY_DREAM_BRIGHT, KEY_DREAM_CLOCK, KEY_DREAM_NOW_PLAYING, KEY_DREAM_OTHER_APPS)
            portable(PrefType.STRING_SET, KEY_NOTIFICATION_BUTTONS)
            portable(PrefType.INT, KEY_DREAM_FPS)
        }
}
