package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative

/**
 * The background image behind the scene: which picture, how it blends into the composite, and
 * how it looks going in (blurred, dimmed).
 *
 * [amount] is the mix strength `NativeViz.setUnderlay` blends at (0 = no underlay effect, 1 =
 * fully blended); [dim] is baked into the decoded pixels themselves before upload (see
 * `BackgroundImage.decode`), so the two compose rather than duplicate one control.
 */
data class BackgroundPrefs(
    val uri: String? = null,
    /** The blend's saved name; `GeodeNative.underlayBlendOf` gives the int the renderer takes. */
    val blend: String = "SCREEN",
    val amount: Float = 0.6f,
    val blurRadius: Int = 0,
    val dim: Float = 0f,
)

class BackgroundPrefsStore(
    private val prefs: SharedPreferences,
) {
    fun load(): BackgroundPrefs {
        val d = BackgroundPrefs()
        return BackgroundPrefs(
            uri = prefs.getString(KEY_URI, null),
            blend = prefs.getString(KEY_BLEND, null) ?: d.blend,
            amount = GeodeNative.underlayClampAmount(prefs.getFloat(KEY_AMOUNT, d.amount)),
            blurRadius = GeodeNative.underlayClampBlur(prefs.getInt(KEY_BLUR, d.blurRadius)),
            dim = GeodeNative.underlayClampDim(prefs.getFloat(KEY_DIM, d.dim)),
        )
    }

    fun save(p: BackgroundPrefs) {
        prefs
            .edit()
            .putString(KEY_URI, p.uri)
            .putString(KEY_BLEND, p.blend)
            .putFloat(KEY_AMOUNT, p.amount)
            .putInt(KEY_BLUR, p.blurRadius)
            .putFloat(KEY_DIM, p.dim)
            .apply()
    }

    companion object {
        /** The blur slider's range; the clamps and the bake are native (core/viz/Underlay), and this reads its bound. */
        val BLUR_RANGE: IntRange by lazy { 0..GeodeNative.underlayBlurMax() }

        private const val KEY_URI = "background_uri"
        private const val KEY_BLEND = "background_blend"
        private const val KEY_AMOUNT = "background_amount"
        private const val KEY_BLUR = "background_blur_radius"
        private const val KEY_DIM = "background_dim"

        internal val PREF_KEYS =
            prefKeys(GeodePrefsFiles.BACKGROUND) {
                device(PrefType.STRING, KEY_URI)
                portable(PrefType.STRING, KEY_BLEND)
                portable(PrefType.FLOAT, KEY_AMOUNT, KEY_DIM)
                portable(PrefType.INT, KEY_BLUR)
            }
    }
}
