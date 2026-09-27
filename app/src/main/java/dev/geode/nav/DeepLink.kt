package dev.geode.nav

/**
 * The ways in from outside: the `geode://` links the manifest claims and Android Auto's
 * play-from-search. Parsed from plain strings so this stays testable without an Intent.
 */
sealed interface DeepLink {
    /** A preset carried whole inside a `geode://preset/…` link. */
    data class Preset(
        val link: String,
    ) : DeepLink

    /** A video template carried inside a `geode://template/…` link. */
    data class Template(
        val link: String,
    ) : DeepLink

    /** "Play <query> on Geode." */
    data class PlayFromSearch(
        val query: String,
    ) : DeepLink

    companion object {
        const val SCHEME = "geode"
        const val ACTION_PLAY_FROM_SEARCH = "android.media.action.MEDIA_PLAY_FROM_SEARCH"

        fun parse(
            action: String?,
            data: String?,
            query: String?,
        ): DeepLink? {
            if (action == ACTION_PLAY_FROM_SEARCH) return PlayFromSearch(query.orEmpty())
            val uri = data ?: return null
            return when {
                uri.startsWith("$SCHEME://preset/", ignoreCase = true) -> Preset(uri)
                uri.startsWith("$SCHEME://template/", ignoreCase = true) -> Template(uri)
                else -> null
            }
        }
    }
}
