package dev.geode.data

import dev.geode.data.prefs.RecordingPrefs
import dev.geode.model.WatermarkChoice
import org.junit.Assert.assertEquals
import org.junit.Test

/** Settings › Preferences › Watermark: nothing stored is today's behaviour, and a choice comes back as made. */
class WatermarkPrefsCodecTest {
    @Test
    fun `nothing stored reads as the tiled mark Pro does not add`() {
        val read = WatermarkPrefsCodec.read(RecordingPrefs().prefs)
        assertEquals(WatermarkChoice(required = false), read)
        assertEquals(WatermarkChoice.TILED, read.placement)
        assertEquals(false, read.proAdds)
    }

    @Test
    fun `a choice round-trips and the entitlement's part is never stored`() {
        val file = RecordingPrefs()
        val choice =
            WatermarkChoice(required = true, proAdds = true, placement = WatermarkChoice.END_CARD, corner = WatermarkChoice.TOP_LEFT)
        file.prefs
            .edit()
            .also { WatermarkPrefsCodec.write(it, choice) }
            .apply()
        assertEquals(choice.copy(required = false), WatermarkPrefsCodec.read(file.prefs))
        assertEquals(WatermarkPrefsCodec.keys.map { it.name }.toSet(), file.written.keys)
    }
}
