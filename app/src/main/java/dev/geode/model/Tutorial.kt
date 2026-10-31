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
    SETTINGS(2),
}

/** One card: which chapter, and which of its cards. */
data class TutorialPosition(
    val chapter: TutorialChapter,
    val card: Int,
)

/**
 * A run of the tour over [chapters], laid out as one flat run of pages.
 *
 * Flat because the carousel is a single pager: swiping across a chapter edge is just the next
 * page, and the pager's own saved page is the whole of the tour's position.
 */
class TutorialTour(
    val chapters: List<TutorialChapter>,
) {
    private val starts: List<Int> = chapters.runningFold(0) { acc, c -> acc + c.cards }

    val pageCount: Int = starts.last()

    fun positionOf(page: Int): TutorialPosition {
        val p = page.coerceIn(0, (pageCount - 1).coerceAtLeast(0))
        val i = (starts.indexOfLast { it <= p }).coerceAtMost(chapters.lastIndex)
        return TutorialPosition(chapters[i], p - starts[i])
    }

    /** The page of [position], or the first page when its chapter is not in this run. */
    fun pageOf(position: TutorialPosition): Int {
        val i = chapters.indexOf(position.chapter)
        if (i < 0) return 0
        return starts[i] + position.card.coerceIn(0, position.chapter.cards - 1)
    }

    /** Where Skip chapter goes from [page]: the next chapter's first page, or null at the last chapter. */
    fun nextChapterPage(page: Int): Int? {
        val i = chapters.indexOf(positionOf(page).chapter)
        return starts[i + 1].takeIf { it < pageCount }
    }

    /**
     * The chapter finished by moving from page [from] to page [to], if any; [to] may be
     * [pageCount], which is Finish.
     *
     * A chapter is done when someone moves forward one page off its last card, having read it.
     * Moving back finishes nothing, and nor does a jump of several pages (Skip chapter from
     * anywhere but the last card): a skipped chapter keeps its row unticked in Settings, which is
     * the reminder that it is still there.
     */
    fun completedBy(
        from: Int,
        to: Int,
    ): TutorialChapter? {
        if (to != from + 1 || from !in 0 until pageCount) return null
        val here = positionOf(from)
        return here.chapter.takeIf { here.card == it.cards - 1 }
    }
}

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
