package dev.geode.model

/** The tour's chapters, in the order it plays them. Settings › Tutorials lists exactly these. */
enum class TourChapter { START, VISUALS, CUSTOMIZE, STUDIO }

/**
 * How a step ends. [POINT] lights a control and waits for Next. [GO] lights a control the user is
 * asked to use, and ends by itself once they have. [WAIT] is a line of text over a usable app,
 * gone once the user has done the thing.
 */
enum class TourStepKind { POINT, GO, WAIT }

/** One step, and the real control it is about. Each chapter's steps play in this order. */
enum class TourSpot(
    val chapter: TourChapter,
    val kind: TourStepKind = TourStepKind.POINT,
) {
    NAV_LIBRARY(TourChapter.START, TourStepKind.GO),
    PLAY_SONG(TourChapter.START, TourStepKind.WAIT),
    NAV_VISUALS(TourChapter.START, TourStepKind.GO),

    VISUALS_TABS(TourChapter.VISUALS),
    STAGE_ARROWS(TourChapter.VISUALS),
    STAGE_TOOLS(TourChapter.VISUALS),
    STAGE_RECORD(TourChapter.VISUALS),

    CUSTOMIZE_KNOBS(TourChapter.CUSTOMIZE),
    CUSTOMIZE_LIST(TourChapter.CUSTOMIZE),
    CUSTOMIZE_RANDOMIZE(TourChapter.CUSTOMIZE),

    STUDIO_RENDER(TourChapter.STUDIO),
    STUDIO_MY_VIDEOS(TourChapter.STUDIO),
}

val TourChapter.spots: List<TourSpot> get() = TourSpot.entries.filter { it.chapter == this }

/** A gesture worth one mention, the first time someone takes the long way round. */
enum class GestureTip { SWIPE_STYLES, DOUBLE_TAP_SHEET, HOLD_TO_RECORD, SWIPE_UP_PLAYER, SWIPE_DOWN_KNOBS, SWIRL_RANDOMIZE }

enum class TourPlace { LIBRARY, VISUALS, ELSEWHERE }

/** What the tour reads from the app: the marked controls laid out now, the screen, the player. */
data class TourFacts(
    val shown: Set<TourSpot> = emptySet(),
    val place: TourPlace = TourPlace.ELSEWHERE,
    val playing: Boolean = false,
)

/**
 * Everything the tour remembers between launches: the chapters seen or skipped, the step an
 * unfinished chapter picks up at, and the gesture tips already said.
 *
 * Plain strings, read leniently: a name a later build dropped is ignored.
 */
data class TourProgress(
    val done: Set<TourChapter> = emptySet(),
    val resume: TourSpot? = null,
    val tips: Set<GestureTip> = emptySet(),
) {
    fun encodeDone(): String = done.joinToString(",") { it.name }

    fun encodeResume(): String? = resume?.name

    fun encodeTips(): String = tips.joinToString(",") { it.name }

    companion object {
        val Finished = TourProgress(done = TourChapter.entries.toSet())

        /**
         * [legacySeen] is the flag older builds kept for "the first-run tour ended". Those installs
         * know the app, so every chapter counts as done and nothing plays on its own.
         */
        fun decode(
            done: String?,
            resume: String?,
            tips: String?,
            legacySeen: Boolean = false,
        ): TourProgress =
            TourProgress(
                done =
                    if (legacySeen) {
                        TourChapter.entries.toSet()
                    } else {
                        names(done).mapNotNull { n -> TourChapter.entries.firstOrNull { it.name == n } }.toSet()
                    },
                resume = TourSpot.entries.firstOrNull { it.name == resume },
                tips = names(tips).mapNotNull { n -> GestureTip.entries.firstOrNull { it.name == n } }.toSet(),
            )

        private fun names(joined: String?) = joined.orEmpty().split(',')
    }
}

/**
 * A run of the tour: the chapters still to play (the head is on screen), the first step of the
 * head not yet passed ([at], an index into its [spots]) and how many pointers it has shown.
 * A [replay] was asked for in Settings and takes the user to each screen in turn.
 */
data class TourRun(
    val chapters: List<TourChapter>,
    val at: Int = 0,
    val seen: Int = 0,
    val replay: Boolean = false,
) {
    val chapter: TourChapter? get() = chapters.firstOrNull()
}

/** The step to draw. [index] of [count] counts pointers only; a [TourStepKind.WAIT] has neither. */
data class TourStep(
    val spot: TourSpot,
    val index: Int,
    val count: Int,
) {
    val last: Boolean get() = index == count
}

/** The tour's rules. Pure: the same run and facts always give the same step. */
object Tour {
    /** Whether the user has already done what [spot] asks. Always false for a [TourStepKind.POINT]. */
    fun reached(
        spot: TourSpot,
        facts: TourFacts,
    ): Boolean =
        when (spot) {
            TourSpot.NAV_LIBRARY -> facts.place != TourPlace.ELSEWHERE || facts.playing
            TourSpot.PLAY_SONG -> facts.playing || facts.place == TourPlace.VISUALS
            TourSpot.NAV_VISUALS -> facts.place == TourPlace.VISUALS
            else -> false
        }

    /** A pointer needs its control on screen; a step already done is never asked for. */
    fun available(
        spot: TourSpot,
        facts: TourFacts,
    ): Boolean =
        when (spot.kind) {
            TourStepKind.POINT -> spot in facts.shown
            TourStepKind.GO -> spot in facts.shown && !reached(spot, facts)
            TourStepKind.WAIT -> !reached(spot, facts)
        }

    /**
     * The step [run] is on: the first one from [TourRun.at] that can be shown. A step whose control
     * is missing is passed over. Null means the chapter has nothing left to show.
     */
    fun step(
        run: TourRun,
        facts: TourFacts,
    ): TourStep? {
        val rest = run.chapter?.spots?.drop(run.at) ?: return null
        val open = rest.filter { available(it, facts) }
        val spot = open.firstOrNull() ?: return null
        return TourStep(spot, index = run.seen + 1, count = run.seen + open.count { it.kind != TourStepKind.WAIT })
    }

    /** [run] with [spot] behind it. */
    fun pass(
        run: TourRun,
        spot: TourSpot,
    ): TourRun =
        run.copy(
            at = spot.chapter.spots.indexOf(spot) + 1,
            seen = run.seen + if (spot.kind == TourStepKind.WAIT) 0 else 1,
        )

    /** [run] on its next chapter, or null when that was the last. */
    fun nextChapter(run: TourRun): TourRun? = run.chapters.drop(1).takeIf { it.isNotEmpty() }?.let { TourRun(it, replay = run.replay) }

    /**
     * The chapter that should start by itself, if any. START leads once, on a new install. Each
     * other chapter plays the first time one of its controls is on screen, once START is done.
     */
    fun due(
        progress: TourProgress,
        facts: TourFacts,
    ): TourChapter? =
        if (TourChapter.START !in progress.done) {
            TourChapter.START
        } else {
            TourChapter.entries.firstOrNull { ch -> ch !in progress.done && ch.spots.any { it in facts.shown } }
        }

    /** A run of [chapter] started by [due], picking up where it was left. */
    fun resume(
        chapter: TourChapter,
        progress: TourProgress,
    ): TourRun = TourRun(listOf(chapter), at = progress.resume?.takeIf { it.chapter == chapter }?.let(chapter.spots::indexOf) ?: 0)

    /** A replay of [chapters] from Settings, in tour order. */
    fun replay(chapters: Collection<TourChapter>): TourRun? =
        TourChapter.entries.filter { it in chapters }.takeIf { it.isNotEmpty() }?.let { TourRun(it, replay = true) }

    /**
     * Progress once [run]'s chapter has ended. A replay changes nothing. Skipping START turns the
     * whole tour off; any other ending ticks that chapter.
     */
    fun ended(
        progress: TourProgress,
        run: TourRun,
        skipped: Boolean,
    ): TourProgress {
        val chapter = run.chapter
        return when {
            run.replay || chapter == null -> progress
            skipped && chapter == TourChapter.START -> progress.copy(done = TourChapter.entries.toSet(), resume = null)
            else -> progress.copy(done = progress.done + chapter, resume = null)
        }
    }

    /**
     * Whether the one-time Pro offer follows this ending: after the first look at Visuals, or at
     * once when the tour was skipped. Never after a replay.
     */
    fun offersPro(
        run: TourRun,
        skipped: Boolean,
    ): Boolean = !run.replay && (run.chapter == TourChapter.VISUALS || (skipped && run.chapter == TourChapter.START))

    /**
     * Where the card goes: the side of the lit control with the most room that fits it, a [gap]
     * away and centred on it; over the control only when no side fits. Always inside [safe].
     * With no control it is centred. Returns the card's left and top.
     */
    fun placeCard(
        hole: TourRect?,
        cardWidth: Int,
        cardHeight: Int,
        safe: TourRect,
        gap: Int,
    ): Pair<Int, Int> {
        val maxX = (safe.right - cardWidth).coerceAtLeast(safe.left)
        val maxY = (safe.bottom - cardHeight).coerceAtLeast(safe.top)
        if (hole == null) return (safe.left + maxX) / 2 to (safe.top + maxY) / 2
        val centredX = ((hole.left + hole.right - cardWidth) / 2).coerceIn(safe.left, maxX)
        val centredY = ((hole.top + hole.bottom - cardHeight) / 2).coerceIn(safe.top, maxY)
        val sides =
            listOf(
                Triple(safe.bottom - hole.bottom - gap - cardHeight, centredX, hole.bottom + gap),
                Triple(hole.top - safe.top - gap - cardHeight, centredX, hole.top - gap - cardHeight),
                Triple(safe.right - hole.right - gap - cardWidth, hole.right + gap, centredY),
                Triple(hole.left - safe.left - gap - cardWidth, hole.left - gap - cardWidth, centredY),
            )
        val best = sides.maxBy { it.first }
        return if (best.first >= 0) best.second to best.third else centredX to maxY
    }
}

/** A rectangle in pixels, for [Tour.placeCard]. */
data class TourRect(
    val left: Int,
    val top: Int,
    val right: Int,
    val bottom: Int,
)
