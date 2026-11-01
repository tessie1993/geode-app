package dev.geode.export

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** A long 4K or 1080p render must come out under MediaMuxer's 32-bit (4 GB) MP4 ceiling. */
class MuxerBudgetTest {
    private val fourK = ExportQuality.UHD4K.bitRate
    private val fullHd = ExportQuality.FHD1080.bitRate

    @Test
    fun `a short render keeps the preset's rate`() {
        val fiveMinutes = minutesUs(5)
        assertEquals(fourK, MuxerBudget.cappedVideoBps(fourK, fiveMinutes, MuxerBudget.audioBytes(fiveMinutes)))
    }

    @Test
    fun `an hour of 4K is capped so video plus audio fit the safe size`() {
        val hour = minutesUs(60)
        val audio = MuxerBudget.audioBytes(hour)
        val capped = MuxerBudget.cappedVideoBps(fourK, hour, audio)
        assertTrue("capped below the preset, was $capped", capped < fourK)
        assertTrue("fits", bytes(capped, hour) + audio <= MuxerBudget.SAFE_BYTES)
        assertTrue("and stays well under 4 GiB", bytes(capped, hour) + audio < FOUR_GIB)
        // Not needlessly low either: within a kilobit of the most that fits.
        assertTrue(bytes(capped + 1_000, hour) + audio > MuxerBudget.SAFE_BYTES)
    }

    @Test
    fun `an hour of 1080p is capped too`() {
        val hour = minutesUs(60)
        val audio = MuxerBudget.audioBytes(hour)
        val capped = MuxerBudget.cappedVideoBps(fullHd, hour, audio)
        assertTrue(capped < fullHd)
        assertTrue(bytes(capped, hour) + audio <= MuxerBudget.SAFE_BYTES)
    }

    @Test
    fun `the cap never goes below the floor`() {
        val tenHours = minutesUs(600)
        assertEquals(MuxerBudget.FLOOR_BPS, MuxerBudget.cappedVideoBps(fourK, tenHours, MuxerBudget.audioBytes(tenHours)))
    }

    @Test
    fun `a request already below the floor is left alone`() {
        val tenHours = minutesUs(600)
        assertEquals(1_000_000, MuxerBudget.cappedVideoBps(1_000_000, tenHours, MuxerBudget.audioBytes(tenHours)))
    }

    @Test
    fun `an unknown duration leaves the request alone`() {
        assertEquals(fourK, MuxerBudget.cappedVideoBps(fourK, 0L, 0L))
        assertEquals(fourK, MuxerBudget.cappedVideoBps(fourK, -1L, 0L))
    }

    @Test
    fun `audio bytes follow the AAC rate`() {
        // 192 kbit/s for one second is 24,000 bytes.
        assertEquals(24_000L, MuxerBudget.audioBytes(1_000_000L))
    }

    private fun minutesUs(minutes: Long): Long = minutes * 60L * 1_000_000L

    private fun bytes(
        bps: Int,
        durationUs: Long,
    ): Long = bps.toLong() * durationUs / 8_000_000L

    private companion object {
        const val FOUR_GIB = 4L * 1024 * 1024 * 1024
    }
}
