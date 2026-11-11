package dev.geode.data.prefs

import android.content.Context
import android.content.ContextWrapper
import android.content.SharedPreferences
import java.lang.reflect.Proxy

/**
 * An in-memory prefs file that remembers every key a store put a value under ([written]), with
 * the platform's rules: a null string removes the key, nothing lands before `apply` or `commit`.
 */
internal class RecordingPrefs {
    val values = mutableMapOf<String, Any>()
    val written = mutableMapOf<String, Any>()

    val prefs: SharedPreferences =
        proxy { name, args ->
            when (name) {
                "edit" -> editor()
                "getAll" -> values
                "contains" -> args[0] in values
                "registerOnSharedPreferenceChangeListener", "unregisterOnSharedPreferenceChangeListener" -> null
                else -> values[args[0]] ?: args[1]
            }
        }

    private fun editor(): SharedPreferences.Editor {
        val pending = mutableListOf<Pair<String, Any?>>()
        lateinit var self: SharedPreferences.Editor
        self =
            proxy { name, args ->
                when (name) {
                    "apply", "commit" -> {
                        for ((key, value) in pending) if (value == null) values.remove(key) else values[key] = value
                        written += pending.mapNotNull { (key, value) -> value?.let { key to it } }
                        (name == "commit").takeIf { it }
                    }
                    "clear" -> self.also { values.keys.forEach { pending += it to null } }
                    "remove" -> self.also { pending += args[0] as String to null }
                    else -> self.also { pending += args[0] as String to args[1] }
                }
            }
        return self
    }

    private inline fun <reified T> proxy(crossinline call: (name: String, args: Array<Any?>) -> Any?): T =
        Proxy.newProxyInstance(T::class.java.classLoader, arrayOf(T::class.java)) { _, method, args ->
            call(method.name, args ?: emptyArray())
        } as T
}

/** A [Context] that hands out [RecordingPrefs] by file name, for the stores that open their own file. */
internal class PrefsContext : ContextWrapper(null) {
    val files = mutableMapOf<String, RecordingPrefs>()

    operator fun get(file: String): SharedPreferences = files.getOrPut(file, ::RecordingPrefs).prefs

    override fun getApplicationContext(): Context = this

    override fun getSharedPreferences(
        name: String,
        mode: Int,
    ): SharedPreferences = get(name)
}
