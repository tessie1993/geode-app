package dev.geode.data

import android.content.SharedPreferences
import dev.geode.export.ExportQuality
import org.junit.Assert.assertEquals
import org.junit.Test
import java.lang.reflect.Proxy

/**
 * Settings › Export › Record's store: a choice never made reads as what a take did before the rows
 * existed (the Custom export defaults, no maximum length), a pick sticks, and a stored value that
 * is not on offer is never handed on. The lengths themselves and the snap are native
 * (core/tests/record_plan_test.cpp); here the snap is a stand-in.
 */
class RecordPrefsStoreTest {
    private val custom = ExportDefaults(quality = ExportQuality.UHD4K, fps = 60)

    private fun store(values: MutableMap<String, Any?> = mutableMapOf()) =
        RecordPrefsStore(mapPrefs(values), snap = { if (it > 45) 60 else 30 })

    @Test
    fun `nothing picked follows the Custom export defaults and has no limit`() {
        assertEquals(RecordPrefs(ExportQuality.UHD4K, 60, maxLengthSec = 0), store().load(custom))
        assertEquals(RecordPrefs(ExportQuality.FHD1080, 30, maxLengthSec = 0), store().load(ExportDefaults()))
        assertEquals(0, store().maxLengthSec())
    }

    @Test
    fun `a pick is kept and no longer follows the Custom defaults`() {
        val values = mutableMapOf<String, Any?>()
        store(values).save(RecordPrefs(ExportQuality.HD720, 24, maxLengthSec = 60))
        assertEquals(RecordPrefsStore.KEYS, values.keys)
        assertEquals(RecordPrefs(ExportQuality.HD720, 24, maxLengthSec = 60), store(values).load(custom))
        assertEquals(60, store(values).maxLengthSec())
    }

    @Test
    fun `a stored value that is not on offer falls back or snaps`() {
        val values = mutableMapOf<String, Any?>("record_quality" to "UHD8K", "record_fps" to 120, "record_max_sec" to 40)
        assertEquals(RecordPrefs(ExportQuality.UHD4K, 60, maxLengthSec = 30), store(values).load(custom))
        values["record_max_sec"] = -7
        assertEquals(0, store(values).maxLengthSec())
    }

    @Test
    fun `a take keeps the Custom ratio, codec and folder and takes the Record size and rate`() {
        val folder = custom.copy(saveTo = SaveTarget.Folder("content://tree/videos"), loopSafe = true)
        val applied = RecordPrefs(ExportQuality.HD720, 25, maxLengthSec = 30).applyTo(folder)
        assertEquals(folder.copy(quality = ExportQuality.HD720, fps = 25), applied)
    }

    /** In-memory [SharedPreferences] over [values]: the platform one is a stub in local unit tests. */
    private fun mapPrefs(values: MutableMap<String, Any?>): SharedPreferences {
        val loader = javaClass.classLoader
        lateinit var editor: SharedPreferences.Editor
        editor =
            Proxy.newProxyInstance(loader, arrayOf(SharedPreferences.Editor::class.java)) { _, method, args ->
                if (method.name.startsWith("put")) values[args[0] as String] = args[1]
                when (method.returnType) {
                    Void.TYPE -> null
                    Boolean::class.javaPrimitiveType -> true
                    else -> editor
                }
            } as SharedPreferences.Editor
        return Proxy.newProxyInstance(loader, arrayOf(SharedPreferences::class.java)) { _, method, args ->
            when (method.name) {
                "edit" -> editor
                "contains" -> args[0] in values
                else -> values[args[0]] ?: args[1]
            }
        } as SharedPreferences
    }
}
