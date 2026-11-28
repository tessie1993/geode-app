package dev.geode.engine.bridge

// The JNI declarations of geode_sound_jni.cpp.
object SoundNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // What the player tells the sound engines that the tap cannot hear (core/sound/PlayerSignals).

    /**
     * A new track (or the same one again) begins a new play with [seed]: every live renderer's sound engine
     * forgets the tempo, key and sections at its next frame and fires a full-strength section change, so a
     * crossfaded or gapless next track starts fresh. Returns the boundary's epoch. Any thread.
     */
    external fun soundTrackBoundary(seed: Long): Long

    /**
     * Whether the player writes the app's ring (true) or the mic or another app's audio has taken it. While
     * the player is the source a pause holds the tempo, key and sections; the others keep the 2 s silence
     * re-warm, and read a fade gain of 1. Any thread.
     */
    external fun soundSetPlayerSource(on: Boolean)

    // Settings › Audio's listening tuning (core/sound/Tuning). The shell keeps only the saved floats, null for
    // the engine's defaults, so a reset follows the engine when they change.

    /** `GEODE_SOUND_TUNING_FLOATS`, then each field's first slot (`core/sound/SoundFrame.h`); a band or drum field takes three. */
    const val SOUND_TUNING_FLOATS = 30
    const val SOUND_TUNING_GAIN = 0
    const val SOUND_TUNING_THRESHOLD = 3
    const val SOUND_TUNING_OFFSET = 6
    const val SOUND_TUNING_ATTACK = 9
    const val SOUND_TUNING_RELEASE = 12
    const val SOUND_TUNING_DRUM = 15
    const val SOUND_TUNING_DRUM_DECAY = 18
    const val SOUND_TUNING_REACTIVITY = 21
    const val SOUND_TUNING_SPEED = 22
    const val SOUND_TUNING_ONSET_THRESHOLD = 23
    const val SOUND_TUNING_HARD_CUT_LEVEL = 24
    const val SOUND_TUNING_HARD_CUT_HALF_LIFE = 25
    const val SOUND_TUNING_PITCH_THRESHOLD = 26
    const val SOUND_TUNING_NOTE_HOLD = 27
    const val SOUND_TUNING_SILENCE_DB = 28
    const val SOUND_TUNING_INPUT_TRIM_DB = 29

    /** Every `GeodeSoundTuning` slot's default, then every minimum, then every maximum; null when out of memory. */
    external fun soundTuningSchema(): FloatArray?

    /** Every slot as the engine hears [saved] (any length, null for none): clamped, or the defaults; null when out of memory. */
    external fun soundTuningResolve(saved: FloatArray?): FloatArray?

    /**
     * The app's own stage (core/viz/StageRun): [flags] are the STAGE_* bits the shell observed; the answer has
     * [STAGE_DRAWS] while somebody can see the stage and [STAGE_CAST_LEADS_AUTO] while a cast's renderer
     * counts the auto-director's style moments in its place.
     */
    external fun stageRun(flags: Int): Int

    const val STAGE_STARTED = 1 shl 0
    const val STAGE_RESUMED = 1 shl 1
    const val STAGE_PIP = 1 shl 2
    const val STAGE_SECOND_SCREEN = 1 shl 3
    const val STAGE_CASTING = 1 shl 4
    const val STAGE_MULTI_WINDOW = 1 shl 5
    const val STAGE_DRAWS = 1 shl 0
    const val STAGE_CAST_LEADS_AUTO = 1 shl 1

    /**
     * Where the stage's view goes (core/viz/StageRun host): [flags] are STAGE_HOST_SHOWN / _CASTING, [bounds] the
     * Visuals rect (left, top, right, bottom; empty before it was laid out), the window [windowW] x [windowH];
     * [rectOut] gets the view's rect. Returns STAGE_HOST_HIDDEN, _FAINT or _OPAQUE.
     */
    external fun stageHost(
        flags: Int,
        bounds: IntArray,
        windowW: Int,
        windowH: Int,
        rectOut: IntArray,
    ): Int

    const val STAGE_HOST_SHOWN = 1 shl 0
    const val STAGE_HOST_CASTING = 1 shl 1
    const val STAGE_HOST_FAINT = 1
    const val STAGE_HOST_OPAQUE = 2

    /** Whether a display with these STAGE_DISPLAY_* bits (and failures) can take the stage (core/viz/StageRun). */
    external fun stageDisplayEligible(flags: Int): Boolean

    /** The index of the connected display the stage shows on ([preferredId] while it qualifies), or -1. */
    external fun stagePickDisplay(
        ids: IntArray,
        flags: IntArray,
        preferredId: Int,
    ): Int

    /** True when the STAGE_PRESENT_* bits all hold: the stage moves to the picked display. */
    external fun stagePresents(flags: Int): Boolean

    const val STAGE_DISPLAY_PRESENTATION = 1 shl 0
    const val STAGE_DISPLAY_DEFAULT = 1 shl 1
    const val STAGE_DISPLAY_HOST = 1 shl 2
    const val STAGE_DISPLAY_ON = 1 shl 3
    const val STAGE_DISPLAY_FAILURE_SHIFT = 8
    const val STAGE_PRESENT_WANTED = 1 shl 0
    const val STAGE_PRESENT_ENTITLED = 1 shl 1
    const val STAGE_PRESENT_FOUND = 1 shl 2

    /**
     * What the stage's next/previous style steps (core/viz/StageStep): [page] is the open Visuals page
     * (STAGE_STEP_PAGE_*), [flags] the STAGE_STEP_* facts and
     * [lookCount] the saved looks the user may apply. One of [STAGE_STEP_STYLES], [STAGE_STEP_LOOKS].
     */
    external fun stageStepTarget(
        page: Int,
        flags: Int,
        lookCount: Int,
    ): Int

    /** The item [step] places from [here] among [open]'s, wrapping, skipping [here] and the closed ones; -1 for none. */
    external fun stageStepIndex(
        open: BooleanArray,
        here: Int,
        step: Int,
    ): Int

    const val STAGE_STEP_PAGE_STYLES = 0
    const val STAGE_STEP_PAGE_CUSTOMIZE = 1
    const val STAGE_STEP_PAGE_PRESETS = 2
    const val STAGE_STEP_PAGE_TEXTURES = 4
    const val STAGE_STEP_PAGE_SETTINGS = 5
    const val STAGE_STEP_SHEET_SHOWN = 1 shl 0
    const val STAGE_STEP_ON_LOOK = 1 shl 2
    const val STAGE_STEP_STYLES = 0
    const val STAGE_STEP_LOOKS = 1

    /**
     * Auto's style timer ([AUTO_TIMER_PLAYLIST], [AUTO_TIMER_RANDOM]; native `core/viz/AutoTimer`): a look
     * just went up at [nowMs], so the wait starts over with a new random spread on top of the interval.
     */
    external fun autoTimerRestart(
        timer: Int,
        nowMs: Long,
    )

    /**
     * Auto is about to bring [key] on (a style id): the switch to it plays Auto's longer
     * fade (native `core/viz/TransitionChoice`).
     */
    external fun autoMarkChange(key: String)

    /** Auto's random draw (core/viz/AutoDraw): an index in 0 until [count], never [onScreen] while count > 1; -1 for none. */
    external fun autoPick(
        count: Int,
        onScreen: Int,
    ): Int

    /**
     * Visuals › Styles' Random (core/viz/StylePick): the index of the style to load among the page's styles,
     * [family] each one's family index and [open] whether the entitlement keeps it; never [onScreen], every family
     * or with [familyOnly] the on-screen style's family alone, a kept style while there is one; -1 for none.
     * With [familyOnly] in a family of one style, [STYLES_ROLL_INSIDE] when that style has a variation to roll
     * ([innerCount], [innerOnScreen] the one showing or -1).
     */
    external fun stylesRandom(
        family: IntArray,
        open: BooleanArray,
        onScreen: Int,
        familyOnly: Boolean,
        innerCount: Int,
        innerOnScreen: Int,
    ): Int

    const val STYLES_ROLL_INSIDE = -2

    /**
     * Visuals > Styles' Random family (core/viz/StylePick): a family other than the one [onScreen] is in (each equally
     * likely), then a style in it; the index among the page's styles, never [onScreen], a kept style while there
     * is one; -1 for none.
     */
    external fun stylesRandomFamily(
        family: IntArray,
        open: BooleanArray,
        onScreen: Int,
    ): Int

    /** The order a Styles list shows its [count] items: [lead] (what is on screen) first, the rest in order. */
    external fun stylesLead(
        count: Int,
        lead: Int,
    ): IntArray

    const val AUTO_TIMER_PLAYLIST = 0
    const val AUTO_TIMER_RANDOM = 1
}
