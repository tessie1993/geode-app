package dev.geode.model

/**
 * The tour's rules for JVM tests, which cannot load libgeode: the step, due, ended and cover rules the
 * state holder's tests lean on, and the prefs' spelling of progress. Native's own rules are
 * core/tests/shell_tour_test.cpp's to prove (every case of the old TourTest is there); a test that reaches
 * a rule not listed here fails.
 */
object TestTourRules : Tour.Rules {
    fun install() = Tour.install(this)

    override fun reached(
        spot: TourSpot,
        facts: TourFacts,
    ): Boolean =
        when (spot) {
            TourSpot.NAV_LIBRARY -> facts.place != TourPlace.ELSEWHERE || facts.playing
            TourSpot.PLAY_SONG -> facts.playing || facts.place == TourPlace.VISUALS
            TourSpot.NAV_VISUALS -> facts.place == TourPlace.VISUALS
            else -> false
        }

    private fun available(
        spot: TourSpot,
        facts: TourFacts,
    ): Boolean =
        when (spot.kind) {
            TourStepKind.POINT -> spot in facts.shown
            TourStepKind.GO -> spot in facts.shown && !reached(spot, facts)
            TourStepKind.WAIT -> !reached(spot, facts)
        }

    override fun step(
        run: TourRun,
        facts: TourFacts,
    ): TourStep? {
        val rest = run.chapter?.spots?.drop(run.at) ?: return null
        val open = rest.filter { available(it, facts) }
        val spot = open.firstOrNull() ?: return null
        return TourStep(spot, index = run.seen + 1, count = run.seen + open.count { it.kind != TourStepKind.WAIT })
    }

    override fun pass(
        run: TourRun,
        spot: TourSpot,
    ): TourRun =
        run.copy(
            at = spot.chapter.spots.indexOf(spot) + 1,
            seen = run.seen + if (spot.kind == TourStepKind.WAIT) 0 else 1,
        )

    override fun nextChapter(run: TourRun): TourRun? {
        val rest = run.chapters.drop(1)
        return if (rest.isEmpty()) null else TourRun(rest, replay = run.replay)
    }

    override fun held(cover: TourCover): Boolean = cover.firstRun || cover.paywall || cover.pip || cover.permissionRationale || cover.away

    override fun shows(
        run: TourRun?,
        progress: TourProgress,
        cover: TourCover,
    ): Boolean = !held(cover) && (run?.replay == true || !progress.off)

    override fun mayTip(
        tip: GestureTip,
        progress: TourProgress,
        cover: TourCover,
        run: TourRun?,
    ): Boolean = tip !in progress.tips && !progress.off && run == null && !held(cover)

    override fun due(
        progress: TourProgress,
        facts: TourFacts,
    ): TourChapter? =
        when {
            progress.off -> null
            TourChapter.START !in progress.done -> TourChapter.START
            else -> TourChapter.entries.firstOrNull { ch -> ch !in progress.done && ch.spots.any { it in facts.shown } }
        }

    override fun resume(
        chapter: TourChapter,
        progress: TourProgress,
    ): TourRun = TourRun(listOf(chapter), at = progress.resume?.takeIf { it.chapter == chapter }?.let(chapter.spots::indexOf) ?: 0)

    override fun replay(chapters: Collection<TourChapter>): TourRun? {
        val ordered = TourChapter.entries.filter { it in chapters }
        return if (ordered.isEmpty()) null else TourRun(ordered, replay = true)
    }

    override fun ended(
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

    override fun placeCard(
        hole: TourRect?,
        cardWidth: Int,
        cardHeight: Int,
        safe: TourRect,
        gap: Int,
    ): Pair<Int, Int> = throw UnsupportedOperationException("native's (shell_tour_test)")

    override fun decode(
        done: String?,
        resume: String?,
        tips: String?,
        legacySeen: Boolean,
        off: Boolean,
    ): TourProgress =
        TourProgress(
            done =
                if (legacySeen) {
                    TourChapter.entries.toSet()
                } else {
                    done
                        .orEmpty()
                        .split(',')
                        .mapNotNull { n -> if (n == "WELCOME") TourChapter.START else TourChapter.entries.firstOrNull { it.name == n } }
                        .toSet()
                },
            resume = TourSpot.entries.firstOrNull { it.name == resume },
            tips =
                tips
                    .orEmpty()
                    .split(',')
                    .mapNotNull { n -> GestureTip.entries.firstOrNull { it.name == n } }
                    .toSet(),
            off = off,
        )

    override fun encodeDone(progress: TourProgress): String = progress.done.joinToString(",") { it.name }

    override fun encodeResume(progress: TourProgress): String? = progress.resume?.name

    override fun encodeTips(progress: TourProgress): String = progress.tips.joinToString(",") { it.name }
}
