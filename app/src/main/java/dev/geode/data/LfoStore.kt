package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.DirectorNative
import dev.geode.engine.bridge.ModulationNative
import dev.geode.util.bestEffort

/**
 * The waves, the envelopes and the reaction routes, each as the text native wrote (core/viz/ModDocument).
 * This keeps the texts and reads none of them: native reads a slot, a field or a route out of one, and an
 * older build's text too, and decides the hand mask ([loadHandMask]); only the prefs reads and writes are here.
 * [readReactions] is native's reading of the routes, which says whether they are still a route list, [handLoad]
 * its reading of the mask; seams for JVM tests, which have no libgeode.
 */
class LfoStore(
    private val prefs: SharedPreferences,
    private val readReactions: (String) -> String? = ModulationNative::modReactionsRead,
    private val handLoad: (String?, Boolean, Int) -> Int = ModulationNative::modHandLoad,
) {
    /** The saved waves; empty when none are, which native reads as every slot off. */
    fun load(): String = prefs.getString(KEY, null).orEmpty()

    fun save(lfos: String) = prefs.edit().putString(KEY, lfos).apply()

    /** The saved envelopes; empty when none are. */
    fun loadAdsrs(): String = prefs.getString(KEY_ADSR, null).orEmpty()

    fun saveAdsrs(adsrs: String) = prefs.edit().putString(KEY_ADSR, adsrs).apply()

    /** The user's own reaction routes, or null while the style's DNA plays (nothing saved, or unreadable). */
    fun loadReactions(): String? = prefs.getString(KEY_REACTIONS, null)?.let(readReactions)

    /**
     * Customize › Music's hand mask, saved beside the routes ([DirectorNative.handSet] says what the bits are); native
     * decides it ([ModulationNative.modHandLoad]). Routes that no longer read (a `reactions/1` list from before the sound
     * engine's sources moved, a newer build's version this one does not know, or damaged text) are no list at all: the
     * DNA plays, so they are dropped here and the mask is 0, or a leftover HAND_ALL would have switched off every
     * director route (#355). Their text is kept whole under [KEY_REACTIONS_UNREAD] first, never destroyed.
     */
    fun loadHandMask(): Int {
        val saved = prefs.getString(KEY_REACTIONS, null)
        val mask = handLoad(saved, prefs.contains(KEY_REACTIONS_HAND), prefs.getInt(KEY_REACTIONS_HAND, 0))
        if (mask >= 0) return mask
        val edit = prefs.edit()
        if (saved != null) edit.putString(KEY_REACTIONS_UNREAD, saved)
        edit.remove(KEY_REACTIONS).remove(KEY_REACTIONS_HAND).apply()
        return 0
    }

    /** Saves [routes] with the mask native decided ([DirectorNative.handSet]); null routes drop both keys. */
    fun saveReactions(
        routes: String?,
        handMask: Int,
    ) {
        val edit = prefs.edit()
        if (routes == null) {
            edit.remove(KEY_REACTIONS).remove(KEY_REACTIONS_HAND)
        } else {
            edit.putString(KEY_REACTIONS, routes).putInt(KEY_REACTIONS_HAND, handMask)
        }
        edit.apply()
    }

    /**
     * Pushes the hand mask process-wide ([DirectorNative.handSet]): every renderer merges the user's routes under
     * it and every native director keeps off those moments. Native counts it only while [hasUser] says a list
     * exists (#355). Called where a surface loads the saved routes on its own (LookStore); the app's own load and
     * edits go through native's held modulation (core/viz/ModState), which pushes it there.
     */
    fun pushHand(
        mask: Int,
        hasUser: Boolean,
    ) = bestEffort("LfoStore", "push hand mask") { DirectorNative.handSet(mask, hasUser) }

    internal companion object {
        const val KEY = "lfos"
        const val KEY_ADSR = "adsrs"
        const val KEY_REACTIONS = "reactions"
        const val KEY_REACTIONS_HAND = "reactions_hand"

        /** Saved routes this build could not read, kept as they were ([loadHandMask]); nothing reads them back yet. */
        const val KEY_REACTIONS_UNREAD = "reactions_unread"

        /** Every key this store writes: a backup files the LFOs, envelopes and reactions under Creative. */
        val KEYS: Set<String> = setOf(KEY, KEY_ADSR, KEY_REACTIONS, KEY_REACTIONS_HAND, KEY_REACTIONS_UNREAD)

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.GENERAL) {
                portable(PrefType.STRING, KEY, KEY_ADSR, KEY_REACTIONS, KEY_REACTIONS_UNREAD)
                portable(PrefType.INT, KEY_REACTIONS_HAND)
            }
    }
}
