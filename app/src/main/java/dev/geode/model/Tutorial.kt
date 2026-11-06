package dev.geode.model

/**
 * The chapters of the new-user tour, in the order it plays them.
 *
 * [cards] is how many cards the chapter has. It lives here, not beside the card text in `ui`,
 * because the tour's arithmetic (where a page lands, what counts as finishing a chapter) is pure
 * and tested off-device; `TutorialCardsTest` holds the two in step.
 *
 * Settings › Tutorials lists exactly these, so each one has to make sense on its own as well as
 * in sequence.
 */
enum class TutorialChapter(
    val cards: Int,
) {
    WELCOME(2),
    PLAYER(3),
    LIBRARY(3),
    VISUALS(3),
    CUSTOMIZE(4),
    LIVE_VIEW(3),
    STUDIO(3),
    ANYWHERE(3),
    PRO(2),
    SETTINGS(2),
}

/** One card: which chapter, and which of its cards. */
data class TutorialPosition(
    val chapter: TutorialChapter,
    val card: Int,
)

/**
 * What the tour remembers between launches: the chapters someone has finished (the ticks in
 * Settings › Tutorials) and, for the first-run tour only, where to pick up after the app was
 * closed mid-tour.
 *
 * Stored as two plain strings rather than JSON, so this stays testable off-device, and read
 * leniently: a chapter renamed or removed in a later build is dropped, not fatal.
 */
data class TutorialProgress(
    val done: Set<TutorialChapter> = emptySet(),
    val resume: TutorialPosition? = null,
) {
    fun encodeDone(): String = done.joinToString(",") { it.name }

    fun encodeResume(): String? = resume?.let { "${it.chapter.name}:${it.card}" }

    companion object {
        fun decode(
            done: String?,
            resume: String?,
        ): TutorialProgress =
            TutorialProgress(
                done =
                    done
                        .orEmpty()
                        .split(',')
                        .mapNotNull(::chapterNamed)
                        .toSet(),
                resume =
                    resume?.split(':')?.takeIf { it.size == 2 }?.let { (name, card) ->
                        val chapter = chapterNamed(name) ?: return@let null
                        card.toIntOrNull()?.let { TutorialPosition(chapter, it) }
                    },
            )

        private fun chapterNamed(name: String) = TutorialChapter.entries.firstOrNull { it.name == name }
    }
}
