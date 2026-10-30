package dev.geode.render.scene

import dev.geode.render.ReactSource

/**
 * Macro sets as text: what the macro folder stores and what Share sends.
 *
 * Plain lines rather than JSON so it parses in local JVM tests (org.json is a stub there; see
 * AGENTS.md) and reads in a message. A free-text name always ends its line, so it may contain
 * spaces. Unknown lines, unknown controls and unknown sounds are skipped rather than failing the
 * set, so an older app opens a newer set with whatever it understands; only a different leading
 * [MAGIC] or a [VERSION] newer than this one is refused.
 *
 * ```
 * mvzmacro 1
 * set Club night
 * macro <bipolar 0|1> <value> <icon> <name>
 * dest <amount> <control name>
 * sound <SOURCE> <amount> <attack s> <release s>
 * ```
 */
object MacroSetCodec {
    const val MAGIC = "mvzmacro"
    const val VERSION = 1

    /** The file extension the folder and Share use. */
    const val EXTENSION = "mvzmacro"

    fun encode(set: MacroSet): String =
        buildString {
            appendLine("$MAGIC $VERSION")
            appendLine("set ${oneLine(set.name)}")
            for (m in set.macros) {
                appendLine("macro ${if (m.bipolar) 1 else 0} ${m.value} ${oneLine(m.icon).replace(' ', '_')} ${oneLine(m.name)}")
                m.destinations.forEach { appendLine("dest ${it.amount} ${oneLine(it.key)}") }
                m.sounds.forEach { appendLine("sound ${it.source.name} ${it.amount} ${it.attackSeconds} ${it.releaseSeconds}") }
            }
        }

    /** The set in [text], or null when it is not a macro set this version can read. */
    fun decode(text: String): MacroSet? {
        val lines = text.lineSequence().map { it.trim() }.filter { it.isNotEmpty() }.toList()
        val header = lines.firstOrNull()?.split(' ') ?: return null
        val version = header.getOrNull(1)?.toIntOrNull()
        if (header.firstOrNull() != MAGIC || version == null || version > VERSION) return null
        var name = ""
        val macros = mutableListOf<UserMacro>()
        for (line in lines.drop(1)) {
            val parts = line.split(' ', limit = 2)
            val rest = parts.getOrElse(1) { "" }
            when (parts[0]) {
                "set" -> name = rest
                "macro" -> parseMacro(rest)?.let(macros::add)
                "dest" -> parseDestination(rest)?.let { d -> macros.updateLast { it.copy(destinations = it.destinations + d) } }
                "sound" -> parseSound(rest)?.let { s -> macros.updateLast { it.copy(sounds = it.sounds + s) } }
            }
        }
        return MacroSet(name, macros)
    }

    private fun parseMacro(rest: String): UserMacro? {
        val f = rest.split(' ', limit = 4)
        if (f.size < 4) return null
        val bipolar = f[0] == "1"
        val value = f[1].toFloatOrNull()?.coerceIn(0f, 1f) ?: return null
        val icon = f[2].takeIf { it in MacroIcons.ALL } ?: MacroIcons.DEFAULT
        return UserMacro(name = f[3], icon = icon, bipolar = bipolar, value = value)
    }

    private fun parseDestination(rest: String): MacroDestination? {
        val f = rest.split(' ', limit = 2)
        val amount = f[0].toFloatOrNull()?.coerceIn(-1f, 1f) ?: return null
        val key = f.getOrNull(1)?.takeIf { MacroParams.of(it) != null } ?: return null
        return MacroDestination(key, amount)
    }

    private fun parseSound(rest: String): MacroSound? {
        val f = rest.split(' ')
        if (f.size < 4) return null
        val source = ReactSource.entries.firstOrNull { it.name == f[0] } ?: return null
        val numbers = f.drop(1).take(3).map { it.toFloatOrNull() ?: return null }
        return MacroSound(
            source = source,
            amount = numbers[0].coerceIn(-1f, 1f),
            attackSeconds = numbers[1].coerceIn(0f, MAX_SECONDS),
            releaseSeconds = numbers[2].coerceIn(0f, MAX_SECONDS),
        )
    }

    private inline fun MutableList<UserMacro>.updateLast(change: (UserMacro) -> UserMacro) {
        if (isNotEmpty()) this[lastIndex] = change(last())
    }

    private fun oneLine(s: String): String = s.replace('\r', ' ').replace('\n', ' ').trim()

    private const val MAX_SECONDS = 10f
}
