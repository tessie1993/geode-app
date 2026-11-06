package dev.geode.analysis

import java.text.Normalizer
import java.util.Locale

/**
 * The app's one text match for search: the library's track list, the global search sheet and
 * voice search all ask it, so "beyonce" finds "Beyoncé" everywhere or nowhere.
 *
 * Matching happens on folded text ([fold]): lower case, accents stripped, a few letters that do not
 * decompose spelled out. Folding is done per field once per library change by the callers that can
 * keep it ([fold] is cheap but not free: a 15k-track library folds four fields per track), and on
 * the fly for the small lists the global search walks.
 *
 * Ranking ([tier]) answers "where did every word land?": a track whose title holds every word is
 * what the user typed, one that needs the artist too comes next, then the album, then anything
 * else. Within a tier the caller's own order stands, so the sort the user picked still means
 * something while searching.
 */
object SearchMatcher {
    const val TRACK_LIMIT = 30

    /** Tier of a track whose title alone holds every term. */
    const val TIER_TITLE = 0

    /** Tier of a track that needs its artist as well as its title. */
    const val TIER_ARTIST = 1

    /** Tier of a track that needs its album too. */
    const val TIER_ALBUM = 2

    /** No match: the item is filtered out. */
    const val NO_MATCH = -1

    fun terms(query: String): List<String> = fold(query).split(SPACES).filter { it.isNotBlank() }

    /**
     * Lower case, accents gone, and the handful of letters NFD does not split spelled out the way
     * people type them on a plain keyboard (ß → ss, æ → ae, ø → o, ł → l, đ → d, œ → oe, þ → th).
     *
     * [Locale.ROOT], never the phone's: in Turkish "I".lowercase() is a dotless ı, and a search box
     * that stops finding "INXS" on a Turkish phone is a bug, not localisation.
     */
    fun fold(text: String): String {
        if (text.isEmpty()) return text
        // Fast path: plain ASCII, the bulk of most libraries' tags, needs only lower-casing.
        if (text.all { it.code < ASCII_LIMIT }) return text.lowercase(Locale.ROOT)
        val decomposed = Normalizer.normalize(text, Normalizer.Form.NFD)
        val out = StringBuilder(decomposed.length)
        for (ch in decomposed) {
            if (Character.getType(ch) == Character.NON_SPACING_MARK.toInt()) continue
            val lower = ch.lowercaseChar()
            out.append(SPELLED_OUT[lower] ?: lower)
        }
        return out.toString()
    }

    /** True when every (folded) term is inside some field. Fields are folded here; see [matchesFolded] to skip that. */
    fun matches(
        terms: List<String>,
        fields: List<String>,
    ): Boolean = matchesFolded(terms, fields.map(::fold))

    /** [matches] over fields the caller already ran through [fold]. */
    fun matchesFolded(
        terms: List<String>,
        foldedFields: List<String>,
    ): Boolean = terms.isNotEmpty() && terms.all { term -> foldedFields.any { it.contains(term) } }

    /**
     * Which field the match needed, as one of the TIER_ constants, or [NO_MATCH].
     *
     * [foldedFields] are in rank order: title, artist, album, then anything else (filename,
     * folder). The tier is the shortest prefix of that list in which every term lands, so a title
     * hit beats an artist hit beats an album hit, and a query split across title and artist
     * ("nirvana teen spirit") ranks as an artist match, below a song actually called that.
     * [termHits] decides whether one term lands in the field at an index; the default is a plain
     * substring, and the library passes its typo-tolerant match.
     */
    fun tier(
        terms: List<String>,
        foldedFields: List<String>,
        termHits: (term: String, fieldIndex: Int) -> Boolean = { term, i -> foldedFields[i].contains(term) },
    ): Int {
        if (terms.isEmpty()) return NO_MATCH
        var needed = 0
        for (term in terms) {
            var at = NO_MATCH
            for (i in foldedFields.indices) {
                if (termHits(term, i)) {
                    at = i
                    break
                }
            }
            if (at == NO_MATCH) return NO_MATCH
            if (at > needed) needed = at
        }
        return needed
    }

    fun <T> filterTracks(
        terms: List<String>,
        items: List<T>,
        max: Int = TRACK_LIMIT,
        uriOf: (T) -> String,
        fieldsOf: (T) -> List<String>,
        preferred: (T) -> Boolean = { false },
    ): List<T> {
        if (terms.isEmpty()) return emptyList()
        val byUri = LinkedHashMap<String, T>()
        val tierOf = HashMap<String, Int>()
        for (item in items) {
            val tier = tier(terms, fieldsOf(item).map(::fold))
            if (tier == NO_MATCH) continue
            val uri = uriOf(item)
            val current = byUri[uri]
            if (current == null || (!preferred(current) && preferred(item))) {
                byUri[uri] = item
                tierOf[uri] = minOf(tier, tierOf[uri] ?: tier)
            }
        }
        // Stable: within a tier the caller's order (recent first, library order) is kept.
        return byUri.entries
            .sortedBy { tierOf[it.key] ?: 0 }
            .take(max)
            .map { it.value }
    }

    private const val ASCII_LIMIT = 0x80

    private val SPACES = Regex("\\s+")

    private val SPELLED_OUT: Map<Char, String> =
        mapOf(
            'ß' to "ss",
            'æ' to "ae",
            'œ' to "oe",
            'ø' to "o",
            'ł' to "l",
            'đ' to "d",
            'ð' to "d",
            'þ' to "th",
            'ı' to "i",
        )
}
