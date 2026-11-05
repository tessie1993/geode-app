---
status: accepted
---

# Studio drops video editing: Render and My videos, plus Record

On 2026-11-05 the owner decided, in epic #227: "Studio is expert only, no video editing: drop video editing. Keep Render, remove Edit entirely. Keep Record." This narrows [ADR 0005](0005-wave-1-restructures-before-release.md)'s Studio part.

What goes:

- The Edit tab (`StudioSection.EDIT`), its home (`ui/EditHome.kt`), the timeline editor (`ui/studio/*`), the old single-clip editor (`ui/StudioScreen.kt`) and its export (`ExportController.startStudioExport`, `StudioExporter.export`), the Studio live preview (`ui/StudioPreview*`) with its native programme clock (`core/studio/PreviewClock`, `geode_preview_*`), and every "Open in Edit".
- `EditorController` no longer opens, edits or undoes a project. It keeps the list of project folders My videos reads.

What stays:

- Studio › Render (one tap, batch, loop) and My videos (renders and takes).
- Record (amended by [ADR 0008](0008-takes-are-screen-captures.md): a take is a screen capture, nothing renders after the stop): a stopped take still becomes a project and renders to MP4 in the background through `ExportController.renderProject`, `export/ProjectComposition` and `StudioExporter.exportComposition`. ADR 0005's project model (`editor/StudioProject`, `data/EditorProjectStore`, `data/TakeStore`) stays as the storage and render plan for takes and kept renders; no screen edits it.
- `ProFeature.STUDIO`, which now gates Record only.

## Consequences

- A Studio tab saved as `EDIT` (in `data/StudioPrefsStore` or a saved instance state) restores to Render.
- Render's "keep it as a project too?" becomes "add it to My videos?": My videos lists renders by the project that holds them.
- ADR 0001's `:feature:studio` row loses `StudioScreen`, `ui/studio/*` and `StudioPreview`.
- The project model still carries editing-era parts (keyframes, markers, templates, transitions) that only the render path reads. Pruning them is separate work, once takes and renders are known to need none of them.
