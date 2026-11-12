package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import dev.geode.model.WatermarkChoice

/**
 * Settings › Preferences › Watermark: [SettingsRepository] over the general prefs (so the manual
 * backup carries it), with [WatermarkPrefsCodec]. Every render pins it once when it starts, through
 * `RenderLimits.withMark`, so a change never reaches a render in progress.
 */
typealias WatermarkPrefsRepository = SettingsRepository<WatermarkChoice>

/**
 * Where the wordmark goes and whether a Pro user adds it. Never set reads as today's behaviour:
 * tiled on a free render, nothing on Pro's. [WatermarkChoice.required] is the entitlement's, never
 * stored; a stored code native does not know reads as the tiled pattern there.
 */
object WatermarkPrefsCodec : PrefsCodec<WatermarkChoice> {
    private const val KEY_PRO_ADDS = "watermark_pro_adds"
    private const val KEY_PLACEMENT = "watermark_placement"
    private const val KEY_CORNER = "watermark_corner"

    override val defaults = WatermarkChoice(required = false)

    override val keys =
        prefKeys(GeodePrefsFiles.GENERAL) {
            portable(PrefType.BOOLEAN, KEY_PRO_ADDS)
            portable(PrefType.INT, KEY_PLACEMENT, KEY_CORNER)
        }

    override fun read(prefs: SharedPreferences): WatermarkChoice =
        WatermarkChoice(
            required = false,
            proAdds = prefs.getBoolean(KEY_PRO_ADDS, defaults.proAdds),
            placement = prefs.getInt(KEY_PLACEMENT, defaults.placement),
            corner = prefs.getInt(KEY_CORNER, defaults.corner),
        )

    override fun write(
        editor: SharedPreferences.Editor,
        value: WatermarkChoice,
    ) {
        editor.putBoolean(KEY_PRO_ADDS, value.proAdds)
        editor.putInt(KEY_PLACEMENT, value.placement)
        editor.putInt(KEY_CORNER, value.corner)
    }
}
