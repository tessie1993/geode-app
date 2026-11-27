package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.BackupClass
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.ShellNative

// What the app does by itself, what each screen does, and what each gesture on the visual means
// (Settings spreads these rows over seven of its tabs). The owner decided every default here (wave 1, 1C). Kept in `geode-prefs`, so
// Settings › Backup carries it in the Settings section.
// "Resume last track on launch" is not here: it is PlayerPrefs.autoResume, which already existed.

/**
 * The screen the app opens on. [LAST_USED] reopens [BehaviourPrefs.lastScreen]. The order is the native ABI
 * (core/shell/Behaviour); stored choices are kept by name.
 */
enum class StartScreen {
    LAST_USED,
    PLAYER,
    LIBRARY,
    VISUALS,
    STUDIO,
}

/**
 * The extra buttons the media notification carries beside play, previous and next. The order is the native
 * ABI (core/shell/Behaviour); stored choices are kept by name.
 */
enum class NotificationButton {
    SHUFFLE,
    REPEAT,
    FAVOURITE,
    ;

    companion object {
        /** The order the session offers [chosen] in (controllers fill their slots from the front): native's. */
        fun ordered(chosen: Set<NotificationButton>): List<NotificationButton> =
            BehaviourRules.reads.buttonsOrdered(mask(chosen)).map { entries[it] }

        /** [chosen] with [button] switched; switching one on past the notification's room switches another off (native). */
        fun toggled(
            chosen: Set<NotificationButton>,
            button: NotificationButton,
            on: Boolean,
        ): Set<NotificationButton> {
            val next = BehaviourRules.reads.buttonsToggled(mask(chosen), button.ordinal, on)
            return entries.filterTo(mutableSetOf()) { next and (1 shl it.ordinal) != 0 }
        }

        private fun mask(chosen: Set<NotificationButton>): Int = chosen.fold(0) { m, b -> m or (1 shl b.ordinal) }
    }
}

/**
 * Behaviour's rules, native's (core/shell/Behaviour through [ShellNative]), asked by enum ordinal: the
 * notification buttons' order and room, and the start screen. JVM tests cannot load libgeode, so they
 * [install] a stand-in.
 */
object BehaviourRules {
    interface Reads {
        fun buttonsOrdered(chosen: Int): IntArray

        fun buttonsToggled(
            chosen: Int,
            button: Int,
            on: Boolean,
        ): Int

        fun lastScreen(last: Int): Int

        fun opensOn(
            start: Int,
            last: Int,
        ): Int
    }

    private object Native : Reads {
        override fun buttonsOrdered(chosen: Int): IntArray = ShellNative.notificationButtonsOrdered(chosen)

        override fun buttonsToggled(
            chosen: Int,
            button: Int,
            on: Boolean,
        ): Int = ShellNative.notificationButtonsToggled(chosen, button, on)

        override fun lastScreen(last: Int): Int = ShellNative.behaviourLastScreen(last)

        override fun opensOn(
            start: Int,
            last: Int,
        ): Int = ShellNative.behaviourOpensOn(start, last)
    }

    @Volatile
    internal var reads: Reads = Native
        private set

    /** Asks [next] from now on. */
    fun install(next: Reads) {
        reads = next
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
 * outside the system gesture insets; the edges stay Android's. The order is the native ABI
 * (`core/viz/GestureMap`); stored choices are kept by name.
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

/**
 * What a [CanvasGesture] does. Not every action suits every gesture; the settings screen offers the ones that do.
 * The order is the native ABI (`core/viz/GestureMap`); stored choices are kept by name.
 */
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

    /** The pool's wakes and stirring the liquid. */
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
    /** Every canvas gesture's action. A gesture missing from the map is [GestureAction.OFF]. */
    val gestures: Map<CanvasGesture, GestureAction> = DEFAULT_GESTURES,
) {
    /** The screen to open on now: native's rule ([BehaviourRules]). */
    val opensOn: StartScreen
        get() = StartScreen.entries[BehaviourRules.reads.opensOn(startScreen.ordinal, lastScreen.ordinal)]

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
 * would pin every default (the gesture map) for users who never chose one.
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
            gestures =
                CanvasGesture.entries.associateWith { gesture ->
                    prefs.enumOf(KEY_GESTURE_PREFIX + gesture.name, d.actionFor(gesture))
                },
        )
    }

    /** [StartScreen.LAST_USED] is a choice, never a screen shown: native reads it as a last screen as the player. */
    override fun normalize(value: BehaviourPrefs): BehaviourPrefs =
        value.copy(lastScreen = StartScreen.entries[BehaviourRules.reads.lastScreen(value.lastScreen.ordinal)])

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
    internal const val KEY_GESTURE_PREFIX = "behaviour_gesture_"

    override val keys =
        prefKeys(GeodePrefsFiles.GENERAL) {
            portable(PrefType.STRING, KEY_START_SCREEN, KEY_LAST_SCREEN, KEY_QUEUE_END)
            add(PrefType.STRING, BackupClass.PORTABLE, CanvasGesture.entries.map { KEY_GESTURE_PREFIX + it.name })
            portable(PrefType.BOOLEAN, KEY_KEEP_SCREEN_ON_VISUALS, KEY_START_ON_HEADPHONES, KEY_REMEMBER_SHUFFLE_REPEAT)
            portable(PrefType.STRING_SET, KEY_NOTIFICATION_BUTTONS)
            retired(
                PrefType.BOOLEAN,
                "behaviour_dream_screen_bright",
                "behaviour_dream_show_clock",
                "behaviour_dream_show_now_playing",
                "behaviour_dream_hear_other_apps",
            )
            retired(PrefType.INT, "behaviour_dream_fps")
        }
}
