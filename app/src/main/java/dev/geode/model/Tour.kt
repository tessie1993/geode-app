package dev.geode.model

import dev.geode.engine.bridge.ShellNative

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
 * What can be over the app. Each one holds every pointer and tip back ([Tour.held]): the tour
 * waits and shows after, so it never lands on something that asked for the user first.
 */
data class TourCover(
    /** The boot intro, the safety notice or first-run setup. */
    val firstRun: Boolean = false,
    val paywall: Boolean = false,
    val pip: Boolean = false,
    /** A screen's own words on why it needs a permission, before or after the system asks. */
    val permissionRationale: Boolean = false,
    /** The app is not in front: a system window (a permission dialog, screen capture consent) is over it. */
    val away: Boolean = false,
)

/**
 * Everything the tour remembers between launches: the chapters seen or skipped, the step an
 * unfinished chapter picks up at, the gesture tips already said, and whether the user turned
 * tips off ([off]: no gesture tip and no chapter of its own accord; a replay still plays).
 *
 * Plain strings in the prefs, read leniently (native's, [Tour.rules]): a name a later build dropped is ignored.
 */
data class TourProgress(
    val done: Set<TourChapter> = emptySet(),
    val resume: TourSpot? = null,
    val tips: Set<GestureTip> = emptySet(),
    val off: Boolean = false,
) {
    fun encodeDone(): String = Tour.rules.encodeDone(this)

    fun encodeResume(): String? = Tour.rules.encodeResume(this)

    fun encodeTips(): String = Tour.rules.encodeTips(this)

    companion object {
        val Finished = TourProgress(done = TourChapter.entries.toSet())

        /**
         * [legacySeen] is the flag older builds kept for "the first-run tour ended". Those installs
         * know the app, so every chapter counts as done and nothing plays on its own. Older builds'
         * WELCOME was the same guided path as [TourChapter.START]; their other chapter names that
         * still exist keep their tick, and their "CHAPTER:card" resume is dropped.
         */
        fun decode(
            done: String?,
            resume: String?,
            tips: String?,
            legacySeen: Boolean = false,
            off: Boolean = false,
        ): TourProgress = Tour.rules.decode(done, resume, tips, legacySeen, off)
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

/**
 * The tour's rules: native's (core/shell/Tour, asked through [ShellNative]). Pure: the same run and
 * facts always give the same step. JVM tests cannot load libgeode, so they [install] a stand-in.
 */
object Tour {
    /** What the tour asks; each is documented on the [Tour] function of the same name. */
    interface Rules {
        fun reached(
            spot: TourSpot,
            facts: TourFacts,
        ): Boolean

        fun step(
            run: TourRun,
            facts: TourFacts,
        ): TourStep?

        fun pass(
            run: TourRun,
            spot: TourSpot,
        ): TourRun

        fun nextChapter(run: TourRun): TourRun?

        fun held(cover: TourCover): Boolean

        fun shows(
            run: TourRun?,
            progress: TourProgress,
            cover: TourCover,
        ): Boolean

        fun mayTip(
            tip: GestureTip,
            progress: TourProgress,
            cover: TourCover,
            run: TourRun?,
        ): Boolean

        fun due(
            progress: TourProgress,
            facts: TourFacts,
        ): TourChapter?

        fun resume(
            chapter: TourChapter,
            progress: TourProgress,
        ): TourRun

        fun replay(chapters: Collection<TourChapter>): TourRun?

        fun ended(
            progress: TourProgress,
            run: TourRun,
            skipped: Boolean,
        ): TourProgress

        fun offersPro(
            run: TourRun,
            skipped: Boolean,
        ): Boolean

        fun placeCard(
            hole: TourRect?,
            cardWidth: Int,
            cardHeight: Int,
            safe: TourRect,
            gap: Int,
        ): Pair<Int, Int>

        fun decode(
            done: String?,
            resume: String?,
            tips: String?,
            legacySeen: Boolean,
            off: Boolean,
        ): TourProgress

        fun encodeDone(progress: TourProgress): String

        fun encodeResume(progress: TourProgress): String?

        fun encodeTips(progress: TourProgress): String
    }

    @Volatile
    internal var rules: Rules = Native
        private set

    /** Asks [next] from now on. */
    fun install(next: Rules) {
        rules = next
    }

    /** Whether the user has already done what [spot] asks. Always false for a [TourStepKind.POINT]. */
    fun reached(
        spot: TourSpot,
        facts: TourFacts,
    ): Boolean = rules.reached(spot, facts)

    /**
     * The step [run] is on: the first one from [TourRun.at] that can be shown. A step whose control
     * is missing is passed over. Null means the chapter has nothing left to show.
     */
    fun step(
        run: TourRun,
        facts: TourFacts,
    ): TourStep? = rules.step(run, facts)

    /** [run] with [spot] behind it. */
    fun pass(
        run: TourRun,
        spot: TourSpot,
    ): TourRun = rules.pass(run, spot)

    /** [run] on its next chapter, or null when that was the last. */
    fun nextChapter(run: TourRun): TourRun? = rules.nextChapter(run)

    /** Whether [cover] holds the tour back: anything over the app does. */
    fun held(cover: TourCover): Boolean = rules.held(cover)

    /** Whether [run] may show a step now: nothing covers the app, and tips are on unless it is a replay. */
    fun shows(
        run: TourRun?,
        progress: TourProgress,
        cover: TourCover,
    ): Boolean = rules.shows(run, progress, cover)

    /** Whether [tip] may be said now: not said before, tips on, no run and nothing over the app. */
    fun mayTip(
        tip: GestureTip,
        progress: TourProgress,
        cover: TourCover,
        run: TourRun?,
    ): Boolean = rules.mayTip(tip, progress, cover, run)

    /**
     * The chapter that should start by itself, if any. START leads once, on a new install. Each
     * other chapter plays the first time one of its controls is on screen, once START is done.
     * None while tips are off.
     */
    fun due(
        progress: TourProgress,
        facts: TourFacts,
    ): TourChapter? = rules.due(progress, facts)

    /** A run of [chapter] started by [due], picking up where it was left. */
    fun resume(
        chapter: TourChapter,
        progress: TourProgress,
    ): TourRun = rules.resume(chapter, progress)

    /** A replay of [chapters] from Settings, in tour order. */
    fun replay(chapters: Collection<TourChapter>): TourRun? = rules.replay(chapters)

    /**
     * Progress once [run]'s chapter has ended. A replay changes nothing. Skipping START turns the
     * whole tour off; any other ending ticks that chapter.
     */
    fun ended(
        progress: TourProgress,
        run: TourRun,
        skipped: Boolean,
    ): TourProgress = rules.ended(progress, run, skipped)

    /**
     * Whether the one-time Pro offer follows this ending: after the first look at Visuals, or at
     * once when the tour was skipped. Never after a replay.
     */
    fun offersPro(
        run: TourRun,
        skipped: Boolean,
    ): Boolean = rules.offersPro(run, skipped)

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
    ): Pair<Int, Int> = rules.placeCard(hole, cardWidth, cardHeight, safe, gap)

    /** The rules as native answers them; runs, progress and facts cross as the ints `geode_shell_api.h` spells. */
    private object Native : Rules {
        override fun reached(
            spot: TourSpot,
            facts: TourFacts,
        ) = ShellNative.tourReached(spot.ordinal, facts.mask(), facts.place.ordinal, facts.playing)

        override fun step(
            run: TourRun,
            facts: TourFacts,
        ) = ShellNative.tourStep(run.wire(), facts.mask(), facts.place.ordinal, facts.playing)?.let {
            TourStep(TourSpot.entries[it[0]], index = it[1], count = it[2])
        }

        override fun pass(
            run: TourRun,
            spot: TourSpot,
        ) = ShellNative.tourPass(run.wire(), spot.ordinal).toRun()

        override fun nextChapter(run: TourRun) = ShellNative.tourNextChapter(run.wire())?.toRun()

        override fun held(cover: TourCover) = ShellNative.tourHeld(cover.mask())

        override fun shows(
            run: TourRun?,
            progress: TourProgress,
            cover: TourCover,
        ) = ShellNative.tourShows(run?.wire(), progress.wire(), cover.mask())

        override fun mayTip(
            tip: GestureTip,
            progress: TourProgress,
            cover: TourCover,
            run: TourRun?,
        ) = ShellNative.tourMayTip(tip.ordinal, progress.wire(), cover.mask(), run?.wire())

        override fun due(
            progress: TourProgress,
            facts: TourFacts,
        ) = TourChapter.entries.getOrNull(ShellNative.tourDue(progress.wire(), facts.mask(), facts.place.ordinal, facts.playing))

        override fun resume(
            chapter: TourChapter,
            progress: TourProgress,
        ) = ShellNative.tourResume(chapter.ordinal, progress.wire()).toRun()

        override fun replay(chapters: Collection<TourChapter>) =
            ShellNative.tourReplay(chapters.fold(0) { mask, chapter -> mask or (1 shl chapter.ordinal) })?.toRun()

        override fun ended(
            progress: TourProgress,
            run: TourRun,
            skipped: Boolean,
        ) = ShellNative.tourEnded(progress.wire(), run.wire(), skipped).toProgress()

        override fun offersPro(
            run: TourRun,
            skipped: Boolean,
        ) = ShellNative.tourOffersPro(run.wire(), skipped)

        override fun placeCard(
            hole: TourRect?,
            cardWidth: Int,
            cardHeight: Int,
            safe: TourRect,
            gap: Int,
        ): Pair<Int, Int> {
            val at = ShellNative.tourPlaceCard(hole?.wire(), cardWidth, cardHeight, safe.wire(), gap)
            return at[0] to at[1]
        }

        override fun decode(
            done: String?,
            resume: String?,
            tips: String?,
            legacySeen: Boolean,
            off: Boolean,
        ) = ShellNative.tourDecode(done, resume, tips, legacySeen, off).toProgress()

        override fun encodeDone(progress: TourProgress) = ShellNative.tourEncode(progress.wire(), ShellNative.TOUR_TEXT_DONE).orEmpty()

        override fun encodeResume(progress: TourProgress) = ShellNative.tourEncode(progress.wire(), ShellNative.TOUR_TEXT_RESUME)

        override fun encodeTips(progress: TourProgress) = ShellNative.tourEncode(progress.wire(), ShellNative.TOUR_TEXT_TIPS).orEmpty()

        private fun TourFacts.mask() = shown.fold(0) { mask, spot -> mask or (1 shl spot.ordinal) }

        private fun TourCover.mask() =
            (if (firstRun) ShellNative.TOUR_COVER_FIRST_RUN else 0) or
                (if (paywall) ShellNative.TOUR_COVER_PAYWALL else 0) or
                (if (pip) ShellNative.TOUR_COVER_PIP else 0) or
                (if (permissionRationale) ShellNative.TOUR_COVER_PERMISSION_RATIONALE else 0) or
                (if (away) ShellNative.TOUR_COVER_AWAY else 0)

        private fun TourRect.wire() = intArrayOf(left, top, right, bottom)

        private fun TourRun.wire() = intArrayOf(at, seen, if (replay) 1 else 0) + chapters.map { it.ordinal }

        private fun IntArray.toRun() =
            TourRun(
                chapters = drop(ShellNative.TOUR_RUN_HEAD).map { TourChapter.entries[it] },
                at = this[0],
                seen = this[1],
                replay = this[2] != 0,
            )

        private fun TourProgress.wire() =
            intArrayOf(if (off) 1 else 0, resume?.ordinal ?: -1, done.size) + done.map { it.ordinal } + tips.map { it.ordinal }

        private fun IntArray.toProgress(): TourProgress {
            val rest = drop(ShellNative.TOUR_PROGRESS_HEAD)
            return TourProgress(
                done = rest.take(this[2]).map { TourChapter.entries[it] }.toSet(),
                resume = TourSpot.entries.getOrNull(this[1]),
                tips = rest.drop(this[2]).map { GestureTip.entries[it] }.toSet(),
                off = this[0] != 0,
            )
        }
    }
}

/** A rectangle in pixels, for [Tour.placeCard]. */
data class TourRect(
    val left: Int,
    val top: Int,
    val right: Int,
    val bottom: Int,
)
