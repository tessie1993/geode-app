package dev.geode.render

/**
 * The saved form of the user's reaction routes: a version line, then one route per line,
 * `enabled|SOURCE|TARGET|MODE|amount|CURVE|attackSeconds|releaseSeconds`.
 *
 * Plain text rather than org.json so it is pure Kotlin and testable off-device (org.json is a
 * stub in local unit tests). Decoding is forgiving the way a store read must be: a file written
 * by a later build, or damaged on disk, loses only what it cannot read.
 * - Missing or unknown version line: null, so the caller falls back to the style's DNA.
 * - A line with the wrong field count, or naming a source or target this build does not know:
 *   that route is dropped (a route without its source or target means nothing).
 * - An unknown mode or curve, or a number that does not parse or is not finite: that field takes
 *   its default. Numbers are clamped to what native accepts.
 */
object ReactionRouteCodec {
    private const val VERSION = "reactions/1"
    private const val SEP = '|'
    private const val FIELDS = 8
    private val DEFAULT = ReactionRoute()

    fun encode(routes: List<ReactionRoute>): String =
        buildString {
            append(VERSION)
            routes.take(ReactionRoute.MAX_ROUTES).forEach { r ->
                append('\n')
                append(
                    listOf(
                        r.enabled,
                        r.source.name,
                        r.target.name,
                        r.mode.name,
                        r.amount,
                        r.curve.name,
                        r.attackSeconds,
                        r.releaseSeconds,
                    ).joinToString(SEP.toString()),
                )
            }
        }

    fun decode(text: String?): List<ReactionRoute>? {
        val lines = text?.lines()?.map { it.trim() } ?: return null
        if (lines.firstOrNull() != VERSION) return null
        return lines
            .drop(1)
            .filter { it.isNotEmpty() }
            .mapNotNull(::decodeRoute)
            .take(ReactionRoute.MAX_ROUTES)
    }

    private fun decodeRoute(line: String): ReactionRoute? {
        val f = line.split(SEP)
        if (f.size != FIELDS) return null
        val source = enumOrNull<ReactSource>(f[1]) ?: return null
        val target = enumOrNull<LfoTarget>(f[2])?.takeIf { it != LfoTarget.NONE } ?: return null
        return ReactionRoute(
            enabled = f[0].toBooleanStrictOrNull() ?: DEFAULT.enabled,
            source = source,
            target = target,
            mode = enumOrNull<ReactMode>(f[3]) ?: DEFAULT.mode,
            amount = number(f[4], DEFAULT.amount).coerceIn(ReactionRoute.MIN_AMOUNT, ReactionRoute.MAX_AMOUNT),
            curve = enumOrNull<ModCurve>(f[5]) ?: DEFAULT.curve,
            attackSeconds = number(f[6], DEFAULT.attackSeconds).coerceIn(0f, ReactionRoute.MAX_ATTACK_SECONDS),
            releaseSeconds = number(f[7], DEFAULT.releaseSeconds).coerceIn(0f, ReactionRoute.MAX_RELEASE_SECONDS),
        )
    }

    private fun number(
        s: String,
        default: Float,
    ): Float = s.toFloatOrNull()?.takeIf { it.isFinite() } ?: default

    private inline fun <reified E : Enum<E>> enumOrNull(name: String): E? = enumValues<E>().firstOrNull { it.name == name }
}
