---
status: accepted
---

# Wave 1 restructures Studio, Visuals and Customize before the release

[ADR 0002](0002-release-before-restructure.md) put the Play release before any restructuring: only the layer fix was to land first, and everything else, Navigation 3 included, was to wait. On 2026-11-02 the owner reviewed the app as a whole. Their verdict: it works but looks messy and unfinished, and Studio is the worst part. Visual and overlay lanes never render, the timeline has no preview, Render never exports the Edit project, a take is look automation rather than a video, and one global "Untitled" project holds everything. **The release is now wave 1, built foundation-first (spec #227), and it rebuilds Studio, Visuals and Customize before anything ships.** This overrides ADR 0002's order for those three areas.

What stays from ADR 0002:

- ADR 0001's module split (steps 2–6) still waits until after the release. Wave 1 changes what users see, not module boundaries. `PlayerSession` stays the god object for now, and the top level keeps its enum navigation. Only the Visuals panel, and later Studio, use Navigation 3 back stacks.
- The layer gate stays enforced in CI.

What wave 1 decides for Studio (part 1D, issue #231):

- **A project is a folder** (`files/projects/<id>/`). It holds its edit, its own export settings, the takes recorded into it and the list of every video rendered from it. Studio opens to the list of these folders. The model is pure Kotlin in `editor/StudioProject.kt`, and storage is `data/EditorProjectStore`.
- **Export settings belong to the project.** Settings › Export keeps defaults per destination (TikTok, Shorts, Feed, Square, YouTube, Master, Custom), and a new project copies its destination's set. Later changes stay on their own side: a change in the project never reaches Settings, and a change in Settings never reaches an existing project. The plain default is 30 fps, the best a free user may render, so a first free render never meets the paywall.
- **A template is a project minus its audio.** Saving one drops the audio lanes, the markers (marks against the song) and the renders. Take clips become plain looks, because a performance belongs to one song. "New from template" fits the template to the new song's length. A **batch** is a template × tracks: one project per track. **Loop / long-form** is a project kind, not a separate render dialog.
- **A take becomes a visual clip on track time.** The recorder is timed by the song position, so pause, seek forward and seek back (a punch-in) keep every change on its beat. Stopping makes a project: the song on an audio lane and the take as a visual clip (a `ClipContent.Scene` carrying a `takeId`), both mapping programme time 0 to the same instant of the song. Take, look and beat-cut clips are the one `Scene` type, so lanes, strips and render code that know a scene clip keep compiling while the render path learns the new kinds.
- **Start fresh.** Wave 1 is pre-launch and only testers have data, so the old `files/editor` ("Untitled") and `files/takes` are deleted, not migrated.

## Consequences

- Parts run in tiers, and each class has one owner at a time (the wave-1 build order). Part 1E (one render path) builds on this model: `StudioProject.edit` says what to draw, `StudioProject.export` how to encode it, and `EditorController.recordRender` records the file.
- The old look-level templates (`data/VideoTemplate`, `TemplateRepository`, the Visuals Templates sheet) stay until part 2F replaces them with project templates. Until then the GLOSSARY's **Template** means a project template, and those files are the legacy format.
- Releases slip by the length of wave 1. The owner accepted this. Wave 2 (3D world, colour system, hi-res audio) waits for after launch.
- The Backup allowlist has to swap `editor` for `projects` and `project-templates` (part 1C owns it).
