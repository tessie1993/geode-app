package dev.geode.export

import android.content.res.Resources
import androidx.annotation.PluralsRes
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative

/**
 * The words for a render's time left. The estimate itself is native (`core/output/RenderEta`, stepped
 * by [ExportRun.publish], pinned by `core/tests/output_test`), and so is which phrase a number of seconds
 * reads as; only the sentences live here.
 */
object RenderEta {
    fun describe(
        res: Resources,
        secondsRemaining: Long,
    ): String {
        val amount = LongArray(1)
        return when (GeodeNative.renderEtaPhrase(secondsRemaining, amount)) {
            GeodeNative.ETA_ALMOST_DONE -> res.getString(R.string.export_eta_almost_done)
            GeodeNative.ETA_ABOUT_A_MINUTE -> res.getString(R.string.export_eta_one_minute)
            GeodeNative.ETA_MINUTES -> count(res, R.plurals.export_eta_minutes, amount[0])
            else -> count(res, R.plurals.export_eta_hours, amount[0])
        }
    }

    private fun count(
        res: Resources,
        @PluralsRes id: Int,
        n: Long,
    ): String = res.getQuantityString(id, n.toInt(), n.toInt())
}
