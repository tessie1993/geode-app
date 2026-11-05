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
 * The work is native (core/viz/Director): the music's feeling decides palette, behaviour and routing over
 * every style. This store only tells the engine how much to drive (the switch, cut to the free taste by
 * [AutoDirectorPolicy]) and which play it is: each new track gets a new seed, so a replay is a new film
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
 * Called on the main thread (the settings screen and [dev.geode.playback.AutoDirectorFeed]).
 */
class AutoDirectorStore private constructor(
    private val prefs: SharedPreferences,
) {
    private val _enabled = MutableStateFlow(prefs.getBoolean(KEY_ENABLED, DEFAULT_ENABLED))
    val enabled: StateFlow<Boolean> = _enabled

    private var seed = Random.nextLong()
    private var gate = FeatureGate.FREE
    private var positionMs = 0L
    private var playing = false
    private var pushed: Triple<Boolean, Float, Long>? = null

    init {
        push()
    }

    fun setEnabled(on: Boolean) {
        _enabled.value = on
        prefs.edit().putBoolean(KEY_ENABLED, on).apply()
        push()
    }

    /** A new track (or the same one again): a new seed, so the director starts the song afresh. */
    fun newPlay(gate: FeatureGate) {
        seed = Random.nextLong()
        this.gate = gate
        positionMs = 0L
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

        @Volatile private var instance: AutoDirectorStore? = null

        fun get(context: Context): AutoDirectorStore =
            instance ?: synchronized(this) {
                instance ?: AutoDirectorStore(GeodePrefsFiles(context.applicationContext).general).also { instance = it }
            }
    }
}
