package dev.geode.data

import android.content.SharedPreferences
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
import org.json.JSONArray
import org.json.JSONObject

class LfoStore(
    private val prefs: SharedPreferences,
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
                .optDouble("rateSeconds", LfoConfig.DEFAULT_RATE_SECONDS.toDouble())
                .toFloat()
                .coerceIn(LfoConfig.MIN_RATE_SECONDS, LfoConfig.MAX_RATE_SECONDS)
        } else {
            val hz = o.optDouble("rateHz", 0.0).toFloat()
            if (hz <= 0f) {
                LfoConfig.DEFAULT_RATE_SECONDS
            } else {
                (1f / hz).coerceIn(LfoConfig.MIN_RATE_SECONDS, LfoConfig.MAX_RATE_SECONDS)
            }
        }

    private companion object {
        const val KEY = "lfos"
        const val KEY_ADSR = "adsrs"
    }
}
