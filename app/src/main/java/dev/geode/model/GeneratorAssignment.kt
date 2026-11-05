package dev.geode.model

/**
 * The generator assignment (#471, #472) as Customize › Environment shows it: which style each of a
 * Scene's four surfaces samples, whether it is pinned, and whether the auto-director may swap it.
 *
 * WHY only a reading: the assignment, its rule and every swap live natively
 * (`core/scene/generator/Assignment`); this is the shell's copy of `geode_viz_generator_text`, parsed
 * here the way the catalogue text is, so the panel draws plain values and the parse is tested off
 * device without `org.json`. The panel never edits this copy: it calls the setters and reads again.
 */
data class GeneratorAssignment(
    val slots: List<GeneratorSlot> = List(SLOT_COUNT) { GeneratorSlot() },
    val policy: Int = POLICY_ONLY_ME,
) {
    companion object {
        /** Sky, wall, material, camera plane: native's `generator::Slot` order (0..3). */
        const val SLOT_COUNT = 4

        /** "Only I change them" (`generator::Policy::OnlyMe`), the default. */
        const val POLICY_ONLY_ME = 0

        /** The auto-director may swap unpinned slots at sections and drops (`Policy::DirectorSwaps`). */
        const val POLICY_DIRECTOR_SWAPS = 1

        /**
         * Reads `geode_viz_generator_text`: one `slot\tid\tpinned` line per slot (slot 0..3, id empty
         * for none, pinned 0/1), then `policy\tn`. A line that does not read is skipped, so an empty
         * or older text leaves every slot empty and the policy at its default.
         */
        fun parse(text: String): GeneratorAssignment {
            val slots = MutableList(SLOT_COUNT) { GeneratorSlot() }
            var policy = POLICY_ONLY_ME
            for (line in text.lineSequence()) {
                val cols = line.trimEnd('\r').split('\t')
                if (cols.size == 2 && cols[0] == "policy") {
                    policy = cols[1].toIntOrNull() ?: policy
                } else if (cols.size == 3) {
                    val slot = cols[0].toIntOrNull()?.takeIf { it in 0 until SLOT_COUNT } ?: continue
                    slots[slot] = GeneratorSlot(styleId = cols[1], pinned = cols[2] == "1")
                }
            }
            return GeneratorAssignment(slots, policy)
        }

        /** Reads `geode_generator_pool_text`: one style id per line, in catalogue order. */
        fun parsePool(text: String): List<String> = text.lineSequence().map { it.trim() }.filter { it.isNotEmpty() }.toList()
    }
}

/** One surface's generator: [styleId] empty when none is assigned. */
data class GeneratorSlot(
    val styleId: String = "",
    val pinned: Boolean = false,
)
