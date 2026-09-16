package dev.geode.data

import android.content.SharedPreferences
import dev.geode.render.UnderlayBlend

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
    val blend: UnderlayBlend = UnderlayBlend.SCREEN,
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
            blend =
                runCatching { UnderlayBlend.valueOf(prefs.getString(KEY_BLEND, null) ?: d.blend.name) }
                    .getOrDefault(d.blend),
            amount = prefs.getFloat(KEY_AMOUNT, d.amount).coerceIn(0f, 1f),
            blurRadius = prefs.getInt(KEY_BLUR, d.blurRadius).coerceIn(BLUR_RANGE),
            dim = prefs.getFloat(KEY_DIM, d.dim).coerceIn(0f, 1f),
        )
    }

    fun save(p: BackgroundPrefs) {
        prefs
            .edit()
            .putString(KEY_URI, p.uri)
            .putString(KEY_BLEND, p.blend.name)
            .putFloat(KEY_AMOUNT, p.amount)
            .putInt(KEY_BLUR, p.blurRadius)
            .putFloat(KEY_DIM, p.dim)
            .apply()
    }

    companion object {
        val BLUR_RANGE: IntRange = 0..25

        private const val KEY_URI = "background_uri"
        private const val KEY_BLEND = "background_blend"
        private const val KEY_AMOUNT = "background_amount"
        private const val KEY_BLUR = "background_blur_radius"
        private const val KEY_DIM = "background_dim"
    }
}
