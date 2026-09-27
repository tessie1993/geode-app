package dev.geode.nav

/**
 * Writes a [NavState] to one string and reads it back, for restore after process death.
 * Everything is written by name and route, never by ordinal, so reordering an enum or a sealed
 * class cannot restore someone onto the wrong page; anything unrecognised is dropped rather than
 * failing the restore, and a stack that lost its root gets it back.
 */
object NavSaver {
    private const val KEY_SECTION = "section"
    private const val KEY_STACK = "stack."
    private const val KEY_OVERLAYS = "overlays"
    private const val KEY_GATE = "gate"
    private const val LIST_SEPARATOR = ","

    fun encode(state: NavState): String =
        buildString {
            appendLine("$KEY_SECTION=${state.section.name}")
            Section.entries.forEach { section ->
                val routes = state.stacks.getValue(section).joinToString(LIST_SEPARATOR) { Routes.encode(it) }
                appendLine("$KEY_STACK${section.name}=$routes")
            }
            appendLine("$KEY_OVERLAYS=${state.overlays.joinToString(LIST_SEPARATOR) { Routes.encode(it) }}")
            state.gate?.let { appendLine("$KEY_GATE=${it.name}") }
        }

    fun decode(saved: String?): NavState {
        if (saved.isNullOrBlank()) return NavState()
        val fields =
            saved
                .lineSequence()
                .filter { '=' in it }
                .associate { it.substringBefore('=') to it.substringAfter('=') }
        val section = fields[KEY_SECTION]?.let { name -> Section.entries.firstOrNull { it.name == name } } ?: Section.PLAYER
        val stacks =
            Section.entries.associateWith { each ->
                val routes =
                    fields[KEY_STACK + each.name]
                        .toRoutes()
                        .mapNotNull(Routes::decode)
                        .filter { it.section == each || it.section == null }
                val root = routes.firstOrNull { it.presentation == Presentation.ROOT } ?: each.root
                listOf(root) + routes.filter { it.presentation != Presentation.ROOT }
            }
        val overlays = fields[KEY_OVERLAYS].toRoutes().mapNotNull(Routes::decodeOverlay).distinct()
        val gate = fields[KEY_GATE]?.let { name -> Gate.entries.firstOrNull { it.name == name } }
        return NavState(section, stacks, overlays, gate)
    }

    private fun String?.toRoutes(): List<String> = orEmpty().split(LIST_SEPARATOR).filter { it.isNotEmpty() }
}
