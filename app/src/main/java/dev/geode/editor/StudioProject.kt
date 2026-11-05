package dev.geode.editor

import dev.geode.data.ExportDefaults
import dev.geode.data.ExportDestination

/*
 * The Studio project model (wave 1, issue #231): what a project is, what a template is, and the
 * three ways one is made (from a template, from a take, as one of a batch).
 *
 * A project is a folder. It holds its edit (timeline, markers, keyframes), its own export settings,
 * the takes recorded into it and the list of every video rendered from it. Studio opens to the list
 * of these folders; there is no global "Untitled" any more. Storage is `data/EditorProjectStore`;
 * nothing here touches a file, so every rule below is plain data in, plain data out.
 *
 * The render path (part 1E) reads a [StudioProject] and nothing else: [StudioProject.edit] says what
 * to draw, [StudioProject.export] how to encode it, and [StudioProject.withRender] records the file.
 *
 * Ids for everything a function creates come from an [IdSource] the caller passes in, for the same
 * reason the timeline operations take theirs: the result is deterministic and the caller owns
 * identity.
 */

@JvmInline
value class ProjectId(
    val value: String,
)

/** Hands out fresh ids. The app uses random UUIDs; a caller that wants repeatable ids passes a counter. */
fun interface IdSource {
    fun next(): String
}

/**
 * What kind of video the project makes. [Standard] renders the programme as laid out. [Loop] is the
 * "Loop / long-form" kind: a short seamless loop drawn once and repeated to [targetLengthMs] (for
 * ambient videos hours long), with its options shown in Render.
 */
sealed interface ProjectKind {
    data object Standard : ProjectKind

    data class Loop(
        val targetLengthMs: Long = DEFAULT_LOOP_LENGTH_MS,
        val loopLengthMs: Long = DEFAULT_LOOP_MS,
    ) : ProjectKind

    companion object {
        const val DEFAULT_LOOP_LENGTH_MS: Long = 60L * 60L * 1000L
        const val DEFAULT_LOOP_MS: Long = 20_000L
    }
}

/**
 * One video rendered from a project. The file itself lives where the user saved it (the Videos
 * library or a chosen folder), so the project keeps a reference, not the bytes: [uri] is the
 * MediaStore or document URI the render wrote.
 */
data class ProjectRender(
    val uri: String,
    val label: String,
    val createdAtMs: Long,
    val durationMs: Long,
)

/** The song a project, a template or a batch is built over. [durationMs] 0 means "not known yet". */
data class TrackRef(
    val uri: String,
    val title: String = "",
    val durationMs: Long = 0L,
)

/**
 * A Studio project: one folder on disk.
 *
 * [export] is the project's own copy of the export settings. It starts as [destination]'s defaults
 * from Settings › Export ([StudioProject.create]) and then belongs to the project: changing it here
 * never touches Settings, and changing Settings never touches an existing project.
 *
 * [templateId] remembers which template the project was made from, for display only; the project
 * holds its own copy of everything the template gave it.
 */
data class StudioProject(
    val id: ProjectId,
    val name: String,
    val kind: ProjectKind = ProjectKind.Standard,
    val destination: ExportDestination = ExportDestination.CUSTOM,
    val export: ExportDefaults = ExportDefaults(),
    val edit: EditorProject = EditorProject(),
    val renders: List<ProjectRender> = emptyList(),
    val createdAtMs: Long = 0L,
    val updatedAtMs: Long = 0L,
    val templateId: ProjectId? = null,
) {
    /**
     * How long the programme runs: the timeline's stated length or its last clip's end, whichever is
     * later. A render of a [ProjectKind.Standard] project is exactly this long.
     */
    val lengthMs: Long
        get() = maxOf(edit.timeline.durationMs, edit.contentEndMs())

    /** Every take recorded into this project, by id, in the order their clips start. */
    val takeIds: List<String>
        get() =
            edit.timeline.lanes
                .flatMap { it.clips }
                .sortedBy { it.startMs }
                .mapNotNull { (it.content as? ClipContent.Scene)?.takeId }
                .distinct()

    /** Newest first, which is how My videos lists them. */
    fun withRender(render: ProjectRender): StudioProject = copy(renders = (listOf(render) + renders.filterNot { it.uri == render.uri }))

    fun withoutRender(uri: String): StudioProject = copy(renders = renders.filterNot { it.uri == uri })

    companion object {
        /**
         * A new empty project for [destination], its export settings copied from [defaults] (the
         * destination's defaults in Settings › Export). Over [track] it gets an audio lane holding
         * the song, so "new project" in Studio is never a blank timeline when a track is playing.
         */
        fun create(
            id: ProjectId,
            name: String,
            destination: ExportDestination,
            defaults: ExportDefaults,
            nowMs: Long,
            ids: IdSource,
            track: TrackRef? = null,
            kind: ProjectKind = ProjectKind.Standard,
        ): StudioProject {
            val timeline =
                if (track == null) {
                    Timeline()
                } else {
                    Timeline(lanes = listOf(audioLane(track, ids)), durationMs = track.durationMs)
                }
            return StudioProject(
                id = id,
                name = name,
                kind = kind,
                destination = destination,
                export = defaults,
                edit = EditorProject(timeline = timeline),
                createdAtMs = nowMs,
                updatedAtMs = nowMs,
            )
        }

        /**
         * The project a stopped recording becomes: the song on an audio lane and the take as one
         * visual clip over it, both on track time.
         *
         * The programme is the span that was recorded: it starts where the recording started in
         * the song ([TakeSpan.trackStartMs]), so the audio clip begins at that point of the track
         * ([Clip.sourceInMs]) and the take clip begins at the take's own start. Both map programme
         * time 0 to the same instant of the song, which is what keeps every style change on the beat
         * it was made on. A take recorded with no track (mic or another app's audio) is the visual
         * clip alone.
         */
        fun fromTake(
            id: ProjectId,
            name: String,
            take: TakeSpan,
            track: TrackRef?,
            destination: ExportDestination,
            defaults: ExportDefaults,
            nowMs: Long,
            ids: IdSource,
        ): StudioProject {
            val clip = take.toClip(ClipId(ids.next()), name)
            val visual = Lane(id = LaneId(ids.next()), kind = LaneKind.Visual, name = name, clips = listOf(clip))
            // The song's full length is often unknown here; the take's end is a floor for it, so the
            // audio can be trimmed back to the song's start but never stretched past what was heard.
            val audio =
                track?.let {
                    Lane(
                        id = LaneId(ids.next()),
                        kind = LaneKind.Audio,
                        name = it.title,
                        clips =
                            listOf(
                                Clip(
                                    id = ClipId(ids.next()),
                                    content = ClipContent.Audio(it.uri),
                                    startMs = 0L,
                                    durationMs = clip.durationMs,
                                    sourceInMs = take.trackStartMs,
                                    sourceDurationMs = maxOf(it.durationMs, take.trackStartMs + clip.durationMs),
                                    label = it.title,
                                ),
                            ),
                    )
                }
            return StudioProject(
                id = id,
                name = name,
                destination = destination,
                export = defaults,
                edit = EditorProject(timeline = Timeline(lanes = listOfNotNull(visual, audio), durationMs = clip.durationMs)),
                createdAtMs = nowMs,
                updatedAtMs = nowMs,
            )
        }

        internal fun audioLane(
            track: TrackRef,
            ids: IdSource,
        ): Lane =
            Lane(
                id = LaneId(ids.next()),
                kind = LaneKind.Audio,
                name = track.title,
                clips =
                    if (track.durationMs < MIN_CLIP_DURATION_MS) {
                        emptyList()
                    } else {
                        listOf(
                            Clip(
                                id = ClipId(ids.next()),
                                content = ClipContent.Audio(track.uri),
                                startMs = 0L,
                                durationMs = track.durationMs,
                                sourceDurationMs = track.durationMs,
                                label = track.title,
                            ),
                        )
                    },
            )
    }
}

/**
 * Where a recorded take sits in its song. [trackStartMs] is the song position the recording began
 * at (after any seek back, the earliest position recorded); [durationMs] is how much song time it
 * covers. [firstSceneId] is the style it opened on.
 */
data class TakeSpan(
    val takeId: String,
    val firstSceneId: String,
    val trackStartMs: Long,
    val durationMs: Long,
) {
    /** The take as a visual clip at programme time 0; its source time is take time. */
    fun toClip(
        id: ClipId,
        label: String,
    ): Clip =
        Clip(
            id = id,
            content = ClipContent.Scene(sceneId = firstSceneId, takeId = takeId),
            startMs = 0L,
            durationMs = durationMs.coerceAtLeast(MIN_CLIP_DURATION_MS),
            sourceDurationMs = durationMs.coerceAtLeast(MIN_CLIP_DURATION_MS),
            label = label,
        )
}

/** Take time (ms from the take's start) shown by this clip at programme time [programmeMs]. */
fun Clip.sourceTimeAt(programmeMs: Long): Long = programmeMs - startMs + sourceInMs

/*
 * Templates.
 */

/**
 * A template: a project minus its audio. It keeps the lanes, looks, takes' places, text, overlays,
 * intro and outro, the kind and the export settings, and drops the song and everything tied to the
 * song (the audio lanes, the markers, which are marks against the music, and the renders).
 *
 * [lengthMs] is the length of the song it was made over. "New from template" fits that length to
 * the new song ([ProjectTemplate.newProject]).
 *
 * Takes are dropped with the song too: a take is a performance of one song and means nothing over
 * another, so a take clip becomes a plain look of the style it opened on.
 */
data class ProjectTemplate(
    val id: ProjectId,
    val name: String,
    val kind: ProjectKind = ProjectKind.Standard,
    val destination: ExportDestination = ExportDestination.CUSTOM,
    val export: ExportDefaults = ExportDefaults(),
    val edit: EditorProject = EditorProject(),
    val lengthMs: Long = 0L,
    val createdAtMs: Long = 0L,
) {
    /**
     * A project over [track]: this template fitted to the song's length, plus an audio lane with the
     * song. Lane, clip and keyframe ids are all fresh, so two projects from one template never share
     * an id. A track of unknown length keeps the template's own length.
     */
    fun newProject(
        id: ProjectId,
        name: String,
        track: TrackRef,
        nowMs: Long,
        ids: IdSource,
    ): StudioProject {
        val length =
            listOf(track.durationMs, lengthMs, edit.contentEndMs()).firstOrNull { it >= MIN_CLIP_DURATION_MS }
                ?: DEFAULT_UNKNOWN_LENGTH_MS
        val fitted = edit.fittedTo(lengthMs, length).withFreshIds(ids)
        val audio = StudioProject.audioLane(track.copy(durationMs = length), ids)
        return StudioProject(
            id = id,
            name = name,
            kind = kind,
            destination = destination,
            export = export,
            edit = fitted.copy(timeline = fitted.timeline.copy(lanes = fitted.timeline.lanes + audio, durationMs = length)),
            createdAtMs = nowMs,
            updatedAtMs = nowMs,
            templateId = this.id,
        )
    }

    /**
     * A batch: one project per track, each this template fitted to its song. Rendering them one
     * after another is the render path's job; this only says what each one is.
     */
    fun batch(
        tracks: List<TrackRef>,
        nowMs: Long,
        ids: IdSource,
        nameFor: (TrackRef) -> String = { it.title.ifBlank { name } },
    ): List<StudioProject> = tracks.map { track -> newProject(ProjectId(ids.next()), nameFor(track), track, nowMs, ids) }
}

/** This project as a template: everything but the song. See [ProjectTemplate]. */
fun StudioProject.toTemplate(
    id: ProjectId,
    name: String,
    nowMs: Long,
): ProjectTemplate {
    val kept = edit.timeline.lanes.filter { it.kind != LaneKind.Audio }
    val keptClips = kept.flatMap { lane -> lane.clips.map { it.id } }.toSet()
    val lanes = kept.map { lane -> lane.withClips(lane.clips.map(::withoutTake)) }
    return ProjectTemplate(
        id = id,
        name = name,
        kind = kind,
        destination = destination,
        export = export,
        edit =
            EditorProject(
                timeline = Timeline(lanes = lanes, durationMs = edit.timeline.durationMs),
                markers = MarkerSet(),
                keyframes = KeyframeSheet().withTracks(edit.keyframes.tracks.filter { it.clipId == null || it.clipId in keptClips }),
            ),
        lengthMs = lengthMs,
        createdAtMs = nowMs,
    )
}

private fun withoutTake(clip: Clip): Clip {
    val scene = clip.content as? ClipContent.Scene ?: return clip
    // As a plain look the clip has no source of its own any more, so it may stretch to a longer song.
    return if (scene.takeId == null) clip else clip.copy(content = scene.copy(takeId = null), sourceInMs = 0L, sourceDurationMs = 0L)
}

/**
 * Fit a template's edit, made over a song of [fromMs], to a song of [toMs].
 *
 * Clips keep their place in time; the song's end is what moves. So:
 * - a clip that ran to the old end (within [END_SNAP_MS]) now runs to the new end, stretched or
 *   cut, as far as its source allows (a video cannot grow past its file);
 * - any other clip that would now run past the end is cut at it;
 * - a clip that starts after the new end is dropped, with its clip keyframes.
 *
 * Programme keyframes past the end are left: they simply never play.
 */
internal fun EditorProject.fittedTo(
    fromMs: Long,
    toMs: Long,
): EditorProject {
    if (fromMs == toMs || toMs <= 0L) return this
    val dropped = mutableSetOf<ClipId>()
    val lanes =
        timeline.lanes.map { lane ->
            lane.withClips(
                lane.clips.mapNotNull { clip ->
                    val fitted =
                        when {
                            clip.startMs >= toMs -> null
                            fromMs > 0L && clip.endMs >= fromMs - END_SNAP_MS -> clip.trimmedToEnd(toMs)
                            clip.endMs > toMs -> clip.trimmedToEnd(toMs)
                            else -> clip
                        }
                    if (fitted == null) dropped += clip.id
                    fitted
                },
            )
        }
    return copy(
        timeline = timeline.copy(lanes = lanes, durationMs = toMs),
        keyframes = KeyframeSheet().withTracks(keyframes.tracks.filterNot { it.clipId != null && it.clipId in dropped }),
    )
}

/**
 * This project as a render capped at [capMs] may draw it: every clip that starts at or after the cap
 * is dropped and every clip that runs past it ends there (its source in-point kept, so the first
 * minute looks and sounds exactly as in the full render). Null, or a programme already within the
 * cap, leaves the project as it is. The cap is the entitlement's ([dev.geode.pro.RenderLimits]);
 * applying it to the edit, before the plan, means the scene segments, footage, captions and the song
 * all stop at the same programme time.
 */
fun StudioProject.cappedAt(capMs: Long?): StudioProject =
    if (capMs == null || capMs <= 0L || lengthMs <= capMs) this else copy(edit = edit.fittedTo(0L, capMs))

/** The last clip end of an edit, 0 when it has no clips. */
internal fun EditorProject.contentEndMs(): Long = timeline.lanes.maxOfOrNull { lane -> lane.clips.maxOfOrNull { it.endMs } ?: 0L } ?: 0L

/**
 * The length a project from a template gets when neither the track nor the template knows one (an
 * empty template over a track whose length is not read yet): long enough to hold the song's clip,
 * which Edit then trims.
 */
internal const val DEFAULT_UNKNOWN_LENGTH_MS: Long = 60_000L

/** How close to the song's end a clip's end counts as "runs to the end" when a template is fitted. */
internal const val END_SNAP_MS: Long = 250L

/** The same edit under fresh lane, clip, marker and keyframe ids, clip keyframes following their clips. */
internal fun EditorProject.withFreshIds(ids: IdSource): EditorProject {
    val clipIds = mutableMapOf<ClipId, ClipId>()
    val lanes =
        timeline.lanes.map { lane ->
            lane.copy(
                id = LaneId(ids.next()),
                clips =
                    lane.clips.map { clip ->
                        val fresh = ClipId(ids.next())
                        clipIds[clip.id] = fresh
                        clip.copy(id = fresh)
                    },
            )
        }
    val tracks =
        keyframes.tracks.map { track ->
            track.copy(
                clipId = track.clipId?.let { clipIds[it] ?: it },
                keys = track.keys.map { it.copy(id = KeyframeId(ids.next())) },
            )
        }
    return copy(
        timeline = timeline.copy(lanes = lanes),
        markers = MarkerSet().addAll(markers.markers.map { it.copy(id = MarkerId(ids.next())) }),
        keyframes = KeyframeSheet().withTracks(tracks),
    )
}
