package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.EnvBand
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.LfoTarget
import dev.geode.render.LfoWave
import dev.geode.render.ModCurve
import dev.geode.render.ModPolarity
import dev.geode.render.ModSource
import dev.geode.render.ModTable
import dev.geode.render.ReactionRoute
import dev.geode.render.bridge.ModConfigCodec
import dev.geode.render.scene.BuildStyleReactionsUseCase
import org.json.JSONArray
import org.json.JSONObject

/** [encodeReactions] and [decodeReactions] are the routes' saved text, native; seams for JVM tests, which have no libgeode. */
class LfoStore(
    private val prefs: SharedPreferences,
    private val encodeReactions: (FloatArray) -> String = GeodeNative::reactionsTextEncode,
    private val decodeReactions: (String) -> FloatArray? = GeodeNative::reactionsTextDecode,
) {
    fun load(): List<LfoConfig> =
        runCatching {
            val raw = prefs.getString(KEY, null) ?: return defaultList()
            val arr = JSONArray(raw)
            (0 until LfoEngine.SLOTS).map { i ->
                if (i < arr.length()) fromJson(arr.getJSONObject(i)) else LfoConfig()
            }
        }.getOrDefault(defaultList())

    fun save(lfos: List<LfoConfig>) {
        val arr = JSONArray()
        lfos.take(LfoEngine.SLOTS).forEach { arr.put(toJson(it)) }
        prefs.edit().putString(KEY, arr.toString()).apply()
    }

    private fun defaultList() = List(LfoEngine.SLOTS) { LfoConfig() }

    fun loadAdsrs(): List<AdsrConfig> =
        runCatching {
            val raw = prefs.getString(KEY_ADSR, null) ?: return List(AdsrEngine.COUNT) { AdsrConfig() }
            val arr = JSONArray(raw)
            (0 until AdsrEngine.COUNT).map { i ->
                if (i < arr.length()) adsrFromJson(arr.getJSONObject(i)) else AdsrConfig()
            }
        }.getOrDefault(List(AdsrEngine.COUNT) { AdsrConfig() })

    fun saveAdsrs(envs: List<AdsrConfig>) {
        val arr = JSONArray()
        envs.take(AdsrEngine.COUNT).forEach { arr.put(adsrToJson(it)) }
        prefs.edit().putString(KEY_ADSR, arr.toString()).apply()
    }

    /** The user's own reaction routes, or null while the style's DNA plays (nothing saved, or unreadable). */
    fun loadReactions(): List<ReactionRoute>? =
        prefs.getString(KEY_REACTIONS, null)?.let(decodeReactions)?.let(ModConfigCodec::unpackReactions)

    /**
     * Customize › Music's hand mask, saved beside the routes ([GeodeNative.handSet] says what the bits are):
     * 0 with no routes, and [GeodeNative.HAND_ALL] for routes saved before the mask existed, which were a
     * whole list of the user's. Routes that no longer decode (a `reactions/1` list from before the sound
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
        routes: List<ReactionRoute>?,
        handMask: Int = GeodeNative.HAND_ALL,
    ) {
        val edit = prefs.edit()
        if (routes == null) {
            edit.remove(KEY_REACTIONS).remove(KEY_REACTIONS_HAND)
        } else {
            edit
                .putString(KEY_REACTIONS, encodeReactions(BuildStyleReactionsUseCase.packExact(routes)))
                .putInt(KEY_REACTIONS_HAND, handMask)
        }
        edit.apply()
    }

    private fun adsrToJson(c: AdsrConfig): JSONObject =
        JSONObject()
            .put("enabled", c.enabled)
            .put("targets", JSONArray().also { a -> c.targets.forEach { a.put(it.name) } })
            .put("attack", c.attack.toDouble())
            .put("decay", c.decay.toDouble())
            .put("sustain", c.sustain.toDouble())
            .put("release", c.release.toDouble())
            .put("amount", c.amount.toDouble())
            .put("band", c.band.name)
            .put("gateThreshold", c.gateThreshold.toDouble())
            .put("sustainTrack", c.sustainTrack)
            .put("retrigger", c.retrigger)
            .put("knob", c.knob)
            .put("moment", c.moment)

    private fun adsrFromJson(o: JSONObject): AdsrConfig =
        AdsrConfig(
            enabled = o.optBoolean("enabled", false),
            targets =
                buildList {
                    val a = o.optJSONArray("targets") ?: JSONArray()
                    for (i in 0 until a.length()) {
                        runCatching { LfoTarget.valueOf(a.getString(i)) }.getOrNull()?.let { add(it) }
                    }
                },
            attack = o.optDouble("attack", 0.05).toFloat(),
            decay = o.optDouble("decay", 0.25).toFloat(),
            sustain = o.optDouble("sustain", 0.5).toFloat(),
            release = o.optDouble("release", 0.35).toFloat(),
            amount = o.optDouble("amount", 0.5).toFloat(),
            band = runCatching { EnvBand.valueOf(o.getString("band")) }.getOrDefault(EnvBand.BASS),
            gateThreshold = o.optDouble("gateThreshold", 0.25).toFloat(),
            sustainTrack = o.optBoolean("sustainTrack", false),
            retrigger = o.optBoolean("retrigger", true),
            knob = o.optInt("knob", 0),
            moment = o.optInt("moment", NO_MOMENT),
        )

    private fun toJson(c: LfoConfig): JSONObject =
        JSONObject()
            .put("enabled", c.enabled)
            .put("source", c.source.name)
            .put("target", c.target.name)
            .put("wave", c.wave.name)
            .put("rateSeconds", c.rateSeconds.toDouble())
            .put("depth", c.depth.toDouble())
            .put("polarity", c.polarity.name)
            .put("curve", c.curve.name)
            .put("beats", c.beats)
            .put("knob", c.knob)
            .put("moment", c.moment)

    private fun fromJson(o: JSONObject): LfoConfig =
        LfoConfig(
            enabled = o.optBoolean("enabled", false),
            source = runCatching { ModSource.valueOf(o.getString("source")) }.getOrDefault(ModSource.LFO),
            target = runCatching { LfoTarget.valueOf(o.getString("target")) }.getOrDefault(LfoTarget.NONE),
            wave = runCatching { LfoWave.valueOf(o.getString("wave")) }.getOrDefault(LfoWave.SINE),
            rateSeconds = readRateSeconds(o),
            depth = o.optDouble("depth", 0.3).toFloat(),
            polarity = runCatching { ModPolarity.valueOf(o.getString("polarity")) }.getOrDefault(ModPolarity.BIPOLAR),
            curve = runCatching { ModCurve.valueOf(o.getString("curve")) }.getOrDefault(ModCurve.LINEAR),
            beats = o.optInt("beats", 0).takeIf { it in LfoConfig.BEAT_CHOICES } ?: 0,
            knob = o.optInt("knob", 0),
            moment = o.optInt("moment", NO_MOMENT),
        )

    /**
     * Reads the rate as a period in seconds, converting a slot saved by an older build.
     *
     * Rates used to be stored in Hz (and could be locked to a detected tempo). A saved
     * `rateHz` is turned into the period it described so an existing setup keeps its speed;
     * the tempo lock is simply dropped, because the slots are free-running now.
     */
    private fun readRateSeconds(o: JSONObject): Float =
        if (o.has("rateSeconds")) {
            o
                .optDouble("rateSeconds", ModTable.defaultRateSeconds.toDouble())
                .toFloat()
                .coerceIn(ModTable.minRateSeconds, ModTable.maxRateSeconds)
        } else {
            val hz = o.optDouble("rateHz", 0.0).toFloat()
            if (hz <= 0f) {
                ModTable.defaultRateSeconds
            } else {
                (1f / hz).coerceIn(ModTable.minRateSeconds, ModTable.maxRateSeconds)
            }
        }

    internal companion object {
        const val KEY = "lfos"
        const val KEY_ADSR = "adsrs"
        const val KEY_REACTIONS = "reactions"
        const val KEY_REACTIONS_HAND = "reactions_hand"

        /** A slot saved before "On a moment" (or set to none): a wave runs free, an envelope fires on a note start. */
        const val NO_MOMENT = -1

        /** Every key this store writes: a backup files the LFOs, envelopes and reactions under Creative. */
        val KEYS: Set<String> = setOf(KEY, KEY_ADSR, KEY_REACTIONS, KEY_REACTIONS_HAND)

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.GENERAL) {
                portable(PrefType.STRING, KEY, KEY_ADSR, KEY_REACTIONS)
                portable(PrefType.INT, KEY_REACTIONS_HAND)
            }
    }
}
