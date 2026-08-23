package dev.geode.data

import android.content.SharedPreferences

class FavouritesStore(
    private val prefs: SharedPreferences,
) {
    private val uris: LinkedHashSet<String> =
        LinkedHashSet(
            prefs
                .getString(KEY, "")
                .orEmpty()
                .split(SEPARATOR)
                .filter { it.isNotBlank() },
        )

    fun isFavourite(uri: String): Boolean = uri in uris

    fun toggle(uri: String): Boolean {
        val added = uri !in uris
        uris.remove(uri)
        if (added) uris.add(uri)
        persist()
        return added
    }

    fun remove(uri: String) {
        if (uris.remove(uri)) persist()
    }

    fun all(): List<String> = uris.toList().asReversed()

    val size: Int get() = uris.size

    private fun persist() {
        prefs.edit().putString(KEY, uris.joinToString(SEPARATOR)).apply()
    }

    private companion object {
        const val KEY = "uris"

        const val SEPARATOR = "\n"
    }
}
