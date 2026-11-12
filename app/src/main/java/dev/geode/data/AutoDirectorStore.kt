package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative
import dev.geode.pro.AutoDirectorPolicy
import dev.geode.pro.FeatureGate
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlin.random.Random

/**
 * The auto-director's switch (Settings › Visual playback) and the play it is directing.
 *
 * The work is native (core/viz/Director): the music's feeling decides behaviour (axes, camera, motion)
 * over every style, never colour or a reaction route. This store only tells the engine how much to
 * drive (the switch, cut to the free taste by [AutoDirectorPolicy]) and which play it is: each new track gets a new seed, so a replay is a new film
 * of the same feeling, and an export started now reads the same seed. Like the macros the configuration
 * is process-wide, so the screen, the wallpaper and the dream all follow it. The switch itself goes
 * native too: an export's renderer pins its director from it once at prepare (geode_viz_pin_engine_settings,
 * director::forExport), so the player moving on (a new seed, or past the free taste) never reaches a render
 * in progress.
 *
 * On by default: Auto is one mode now, the director driving the look with an optional timed style
 * change on top (Auto visuals' timer, `AutoVisualsPrefsStore`), and a fresh install
 * starts with the director part on. A free user gets the first minute of each song ([AutoDirectorPolicy]).
 *
 * How far it reaches past its look is [reach] (Visuals › Settings, under Auto Director): the levels and what each
 * does are native (core/viz/DirectorReach); this store keeps the choice and hands it over, with Customize's
 * keep set, which the reach never moves.
 *
 * One instance per process, built by `GeodeContainer`: the switch and the reach are kept by
 * [AutoDirectorCodec] in a [SettingsRepository] (#1009, PR 6), read on IO, and nothing is pushed before
 * that read is back, so a saved "off" is never preceded by the default. Called on the main thread (the
 * settings screen and [dev.geode.playback.AutoDirectorFeed]).
 */
class AutoDirectorStore internal constructor(
    private val repository: SettingsRepository<AutoDirectorSettings>,
    private val scope: CoroutineScope,
    /** Customize's keep set as saved ([KEY_KEPT]), read once (on IO) when the store starts. */
    keptAtStart: suspend () -> Set<String>,
) {
    val enabled: StateFlow<Boolean> =
        repository.value
            .map { it.enabled }
            .stateIn(scope, SharingStarted.Eagerly, repository.value.value.enabled)

    val reach: StateFlow<Int> =
        repository.value
            .map { it.reach }
            .stateIn(scope, SharingStarted.Eagerly, repository.value.value.reach)

    private var seed = Random.nextLong()
    private var gate = FeatureGate.FREE
    private var positionMs = 0L
    private var playing = false
    private var loaded = false
    private var pushed: Triple<Boolean, Float, Long>? = null
    private var pushedReach: Int? = null

    init {
        scope.launch {
            setKept(keptAtStart())
            repository.loaded()
            loaded = true
            repository.value.collect {
                push()
                pushReach()
            }
        }
    }

    /** The native level (GeodeNative.directorReachSet): 0 the look alone, 1 shuffle, 2 shuffle and modulate. */
    fun setReach(level: Int) {
        scope.launch { repository.update { it.copy(reach = level) } }
    }

    /**
     * Customize's locked controls and kept categories, as saved under [KEY_KEPT]: the saved set at start, then
     * each change `ModulationController` saves. The one writer of the native keep set.
     */
    fun setKept(keys: Set<String>) {
        bestEffort(TAG, "push keep set") { GeodeNative.directorKeepSet(keys.joinToString("\n")) }
    }

    private fun pushReach() {
        val level = repository.value.value.reach
        if (level == pushedReach) return
        pushedReach = level
        bestEffort(TAG, "push reach") { GeodeNative.directorReachSet(level) }
    }

    fun setEnabled(on: Boolean) {
        scope.launch { repository.update { it.copy(enabled = on) } }
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

    /** The saved switch only, never its default: until the read is back the engine keeps what it has. */
    private fun push() {
        if (!loaded) return
        val on = repository.value.value.enabled
        val amount = AutoDirectorPolicy.amount(on, gate, positionMs, playing)
        val next = Triple(on, amount, seed)
        if (next == pushed) return
        pushed = next
        bestEffort(TAG, "push director") { GeodeNative.directorSet(on, amount, seed) }
    }

    companion object {
        private const val TAG = "AutoDirectorStore"
        const val DEFAULT_ENABLED = true
        const val DEFAULT_REACH = 0

        /** Customize's lock set in the modulation prefs, which `ModulationController` edits. */
        const val KEY_KEPT = "locked_params"
    }
}

/** The director's switch and reach as the user set them. */
data class AutoDirectorSettings(
    val enabled: Boolean = AutoDirectorStore.DEFAULT_ENABLED,
    val reach: Int = AutoDirectorStore.DEFAULT_REACH,
)

/** How [AutoDirectorSettings] sit in `geode-prefs`. */
object AutoDirectorCodec : PrefsCodec<AutoDirectorSettings> {
    private const val KEY_ENABLED = "auto_director_enabled"
    private const val KEY_REACH = "auto_director_reach"

    override val defaults = AutoDirectorSettings()

    override val keys =
        prefKeys(GeodePrefsFiles.GENERAL) {
            portable(PrefType.BOOLEAN, KEY_ENABLED)
            portable(PrefType.INT, KEY_REACH)
        }

    override fun read(prefs: SharedPreferences) =
        AutoDirectorSettings(prefs.getBoolean(KEY_ENABLED, defaults.enabled), prefs.getInt(KEY_REACH, defaults.reach))

    override fun write(
        editor: SharedPreferences.Editor,
        value: AutoDirectorSettings,
    ) {
        editor.putBoolean(KEY_ENABLED, value.enabled).putInt(KEY_REACH, value.reach)
    }

    /** Customize's keep set as `ModulationController` saved it in [modulation], for the store's start. */
    fun readKept(modulation: SharedPreferences): Set<String> =
        modulation.getStringSet(AutoDirectorStore.KEY_KEPT, emptySet()).orEmpty().toSet()
}
