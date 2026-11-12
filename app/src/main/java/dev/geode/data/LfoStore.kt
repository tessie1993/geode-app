package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort

/**
 * The waves, the envelopes and the reaction routes, each as the text native wrote (core/viz/ModDocument).
 * This keeps the texts and reads none of them: native reads a slot, a field or a route out of one, and an
 * older build's text too. [readReactions] is native's reading of the routes, which says whether they are
 * still a route list; a seam for JVM tests, which have no libgeode.
 */
class LfoStore(
    private val prefs: SharedPreferences,
    private val readReactions: (String) -> String? = GeodeNative::modReactionsRead,
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
     * Customize › Music's hand mask, saved beside the routes ([GeodeNative.handSet] says what the bits are):
     * 0 with no routes, and [GeodeNative.HAND_ALL] for routes saved before the mask existed, which were a
     * whole list of the user's. Routes that no longer read (a `reactions/1` list from before the sound
     * engine's sources moved, or damaged text) are no list at all: the DNA plays, so they are dropped here
     * and the mask is 0, or a leftover HAND_ALL would have switched off every director route (#355).
     */
    fun loadHandMask(): Int {
        if (loadReactions() != null) return prefs.getInt(KEY_REACTIONS_HAND, GeodeNative.HAND_ALL)
        if (prefs.contains(KEY_REACTIONS)) {
            prefs
                .edit()
                .remove(KEY_REACTIONS)
                .remove(KEY_REACTIONS_HAND)
                .apply()
        }
        return 0
    }

    fun saveReactions(
        routes: String?,
        handMask: Int = GeodeNative.HAND_ALL,
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
     * Pushes the hand mask process-wide ([GeodeNative.handSet]): every renderer merges the user's routes under
     * it and every native director keeps off those moments. Native counts it only while [hasUser] says a list
     * exists (#355). Called where the shell loads or edits the user's routes.
     */
    fun pushHand(
        mask: Int,
        hasUser: Boolean,
    ) = bestEffort("LfoStore", "push hand mask") { GeodeNative.handSet(mask, hasUser) }

    internal companion object {
        const val KEY = "lfos"
        const val KEY_ADSR = "adsrs"
        const val KEY_REACTIONS = "reactions"
        const val KEY_REACTIONS_HAND = "reactions_hand"

        /** Every key this store writes: a backup files the LFOs, envelopes and reactions under Creative. */
        val KEYS: Set<String> = setOf(KEY, KEY_ADSR, KEY_REACTIONS, KEY_REACTIONS_HAND)

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.GENERAL) {
                portable(PrefType.STRING, KEY, KEY_ADSR, KEY_REACTIONS)
                portable(PrefType.INT, KEY_REACTIONS_HAND)
            }
    }
}
