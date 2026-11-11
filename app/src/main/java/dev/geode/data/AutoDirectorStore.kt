package dev.geode.data

import android.content.Context
import android.content.SharedPreferences
import dev.geode.engine.bridge.GeodeNative
import dev.geode.pro.AutoDirectorPolicy
import dev.geode.pro.FeatureGate
import dev.geode.util.bestEffort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlin.random.Random

/**
 * The auto-director's switch (Settings › Behaviour) and the play it is directing.
 *
 * The work is native (core/viz/Director): the music's feeling decides behaviour (axes, camera, motion)
 * over every style, never colour or a reaction route. This store only tells the engine how much to
 * drive (the switch, cut to the free taste by [AutoDirectorPolicy]) and which play it is: each new track gets a new seed, so a replay is a new film
 * of the same feeling, and an export started now reads the same seed. Like the macros the configuration
 * is process-wide, so the screen, the wallpaper and the dream all follow it; [GeodeApp] creates the store
 * at start, so a wallpaper that starts before the app does still sees the switch. The switch itself goes
 * native too: an export's renderer pins its director from it once at prepare (geode_viz_pin_engine_settings,
 * director::forExport), so the player moving on (a new seed, or past the free taste) never reaches a render
 * in progress.
 *
 * On by default: Auto is one mode now, the director driving the look with an optional timed style
 * change on top (Auto visuals' timer, `AutoVisualsPrefsStore`), and a fresh install
 * starts with the director part on. A free user gets the first minute of each song ([AutoDirectorPolicy]).
 *
 * How far it reaches past its look is [reach] (Visuals › Settings, under Auto): the levels and what each
 * does are native (core/viz/DirectorReach); this store keeps the choice and hands it over, with Customize's
 * keep set, which the reach never moves.
 *
 * Called on the main thread (the settings screen and [dev.geode.playback.AutoDirectorFeed]).
 */
class AutoDirectorStore private constructor(
    private val prefs: SharedPreferences,
    kept: Set<String>,
) {
    private val _enabled = MutableStateFlow(prefs.getBoolean(KEY_ENABLED, DEFAULT_ENABLED))
    val enabled: StateFlow<Boolean> = _enabled

    private val _reach = MutableStateFlow(prefs.getInt(KEY_REACH, DEFAULT_REACH))
    val reach: StateFlow<Int> = _reach

    private var seed = Random.nextLong()
    private var gate = FeatureGate.FREE
    private var positionMs = 0L
    private var playing = false
    private var pushed: Triple<Boolean, Float, Long>? = null

    init {
        push()
        pushReach()
        setKept(kept)
    }

    /** The native level (GeodeNative.directorReachSet): 0 the look alone, 1 shuffle, 2 shuffle and modulate. */
    fun setReach(level: Int) {
        _reach.value = level
        prefs.edit().putInt(KEY_REACH, level).apply()
        pushReach()
    }

    /** Customize's locked controls and kept categories, as saved under [KEY_KEPT]. */
    fun setKept(keys: Set<String>) {
        bestEffort(TAG, "push keep set") { GeodeNative.directorKeepSet(keys.joinToString("\n")) }
    }

    private fun pushReach() {
        bestEffort(TAG, "push reach") { GeodeNative.directorReachSet(_reach.value) }
    }

    fun setEnabled(on: Boolean) {
        _enabled.value = on
        prefs.edit().putBoolean(KEY_ENABLED, on).apply()
        push()
    }

    /**
     * A new track (or the same one again): a new seed, so the director starts the song afresh, and a track
     * boundary with that seed for every live sound engine (core/sound/PlayerSignals), so the new song's
     * tempo, key and sections are its own even when it follows gapless or crossfaded, which the tap cannot
     * hear.
     */
    fun newPlay(gate: FeatureGate) {
        seed = Random.nextLong()
        this.gate = gate
        positionMs = 0L
        bestEffort(TAG, "track boundary") { GeodeNative.soundTrackBoundary(seed) }
        push()
    }

    /** Where playback is, for the free taste; pushes only when the amount changes. */
    fun onPosition(
        positionMs: Long,
        gate: FeatureGate,
    ) {
        this.positionMs = positionMs
        this.gate = gate
        push()
    }

    /**
     * Whether the app's own player is playing. The free taste is timed by its position, so it runs only
     * while it plays; the mic and other apps' audio pause the player and get no taste.
     */
    fun onPlaying(
        playing: Boolean,
        gate: FeatureGate,
    ) {
        this.playing = playing
        this.gate = gate
        push()
    }

    private fun push() {
        val amount = AutoDirectorPolicy.amount(_enabled.value, gate, positionMs, playing)
        val next = Triple(_enabled.value, amount, seed)
        if (next == pushed) return
        pushed = next
        bestEffort(TAG, "push director") { GeodeNative.directorSet(_enabled.value, amount, seed) }
    }

    companion object {
        private const val TAG = "AutoDirectorStore"
        private const val KEY_ENABLED = "auto_director_enabled"
        const val DEFAULT_ENABLED = true
        private const val KEY_REACH = "auto_director_reach"
        const val DEFAULT_REACH = 0

        /** Customize's lock set in the modulation prefs, which `ModulationController` edits. */
        const val KEY_KEPT = "locked_params"

        @Volatile private var instance: AutoDirectorStore? = null

        fun get(context: Context): AutoDirectorStore =
            instance ?: synchronized(this) {
                instance ?: create(GeodePrefsFiles(context.applicationContext)).also { instance = it }
            }

        private fun create(files: GeodePrefsFiles): AutoDirectorStore =
            AutoDirectorStore(files.general, files.modulation.getStringSet(KEY_KEPT, emptySet()) ?: emptySet())
    }
}
