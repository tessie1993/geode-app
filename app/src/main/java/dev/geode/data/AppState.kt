package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.BackupClass
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import dev.geode.model.TourProgress

/**
 * What the app remembers about this install, as opposed to a choice: no Settings row, never
 * restored by a backup (#1009, PR 5; was part of `GuiPrefs`).
 */
data class AppState(
    /**
     * Whether the photosensitivity notice has been seen. It is an acknowledgement, not a choice:
     * the flash clamp is unconditional either way, so this only decides whether the notice shows.
     */
    val safetyAcknowledged: Boolean = false,
    /** Whether first-run setup has been completed. */
    val setupDone: Boolean = false,
    /**
     * The tour: chapters seen or skipped, where an unfinished one picks up, the tips said, and
     * Settings › Tutorials' tips switch, which stays with this install like the rest of it.
     */
    val tourProgress: TourProgress = TourProgress(),
)

typealias AppStateRepository = SettingsRepository<AppState>

/** How [AppState] sits in `geode-prefs`, under the keys `GuiPrefs` used, with the answers older builds left. */
object AppStateCodec : PrefsCodec<AppState> {
    internal const val KEY_SAFETY_ACKNOWLEDGED = "gui_safety_acknowledged"
    internal const val KEY_SETUP_DONE = "gui_setup_done"
    internal const val KEY_TUTORIAL_DONE = "gui_tutorial_done"
    internal const val KEY_TUTORIAL_RESUME = "gui_tutorial_resume"
    internal const val KEY_TUTORIAL_TIPS = "gui_tutorial_tips"
    internal const val KEY_TUTORIAL_OFF = "gui_tutorial_off"

    /** Read-only: the retired first-run answer, kept solely so an old install counts as set up. */
    internal const val KEY_INTENT = "gui_intent"

    /** Read-only: the old three-way safety answer, kept solely to migrate existing installs. */
    internal const val KEY_SAFETY_CHOICE = "gui_safety_choice"

    /** The old "tour ended" flag, folded into the chapters on the first write after it is read. */
    internal const val KEY_TUTORIAL_SEEN = "gui_tutorial_seen"
    private const val KEY_TUTORIAL_ON_FIRST_RUN = "gui_tutorial_on_first_run"

    override val defaults = AppState()

    override val keys =
        prefKeys(GeodePrefsFiles.GENERAL) {
            state(PrefType.BOOLEAN, KEY_SAFETY_ACKNOWLEDGED, KEY_SETUP_DONE, KEY_TUTORIAL_OFF)
            state(PrefType.STRING, KEY_TUTORIAL_DONE, KEY_TUTORIAL_RESUME, KEY_TUTORIAL_TIPS)
            retired(PrefType.STRING, KEY_INTENT, KEY_SAFETY_CHOICE)
            retired(PrefType.BOOLEAN, KEY_TUTORIAL_SEEN, backup = BackupClass.STATE)
            retired(null, KEY_TUTORIAL_ON_FIRST_RUN)
        }

    override fun read(prefs: SharedPreferences) =
        AppState(
            // Anyone who answered the old safety question has already been shown the notice.
            safetyAcknowledged = prefs.getBoolean(KEY_SAFETY_ACKNOWLEDGED, false) || prefs.getString(KEY_SAFETY_CHOICE, null) != null,
            // Anyone who answered the old "what did you come here to do" had finished setup, but only
            // while the explicit flag was never written: older builds wrote the intent on every save.
            setupDone =
                if (prefs.contains(KEY_SETUP_DONE)) {
                    prefs.getBoolean(KEY_SETUP_DONE, false)
                } else {
                    prefs.getString(KEY_INTENT, null) != null
                },
            tourProgress =
                TourProgress.decode(
                    done = prefs.getString(KEY_TUTORIAL_DONE, null),
                    resume = prefs.getString(KEY_TUTORIAL_RESUME, null),
                    tips = prefs.getString(KEY_TUTORIAL_TIPS, null),
                    legacySeen = prefs.getBoolean(KEY_TUTORIAL_SEEN, false),
                    off = prefs.getBoolean(KEY_TUTORIAL_OFF, false),
                ),
        )

    override fun write(
        editor: SharedPreferences.Editor,
        value: AppState,
    ) {
        editor
            .putBoolean(KEY_SAFETY_ACKNOWLEDGED, value.safetyAcknowledged)
            .putBoolean(KEY_SETUP_DONE, value.setupDone)
            .putString(KEY_TUTORIAL_DONE, value.tourProgress.encodeDone())
            .putString(KEY_TUTORIAL_RESUME, value.tourProgress.encodeResume())
            .putString(KEY_TUTORIAL_TIPS, value.tourProgress.encodeTips())
            .putBoolean(KEY_TUTORIAL_OFF, value.tourProgress.off)
            .remove(KEY_TUTORIAL_SEEN)
            .remove(KEY_TUTORIAL_ON_FIRST_RUN)
    }
}
