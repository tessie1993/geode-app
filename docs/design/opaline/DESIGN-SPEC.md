# Geode / Opaline: product design and wiring specification

Status: approved scope from the user's request; implementation and device evidence remain separate. This is the source of truth for the new interface, navigation, material behavior, and agent handoffs. Read it with [PLAN.md](PLAN.md), [LIBRARY-AUDIT.md](LIBRARY-AUDIT.md), and [VIDEO-REFERENCES.md](VIDEO-REFERENCES.md).

## Source and design intent

The supplied Opaline library is a component and rendering kit, not an Android screen template. Its 247 catalogue entries contain 157 authored mesh assemblies plus optical, physics, and assembly contracts with varying implementation status. The 76 compositions are text-free. Native Geode labels, inputs, accessibility semantics, media actions, and user data supply their meaning. The three supplied videos establish the desired **visible mass**: a large refractive player orb and compressing puck, rounded floating playlist objects, and a cyan/lavender library that changes into a droplet-led player. Frame citations are in VIDEO-REFERENCES.md. The library's `START-HERE`, `COMPONENT-ANATOMY`, `ASSEMBLY-API`, `OPTICS`, and `TRANSITIONS` documents define what its code actually does.

The visual test is a three-quarter camera view of a working app screen. The viewer should see the front, shoulders, sidewalls, undersides, cast shadows, transmitted light, and a coherent receiving plane. Flat colored Compose cards in front of invisible 3D objects fail that test. A video or PNG printed behind controls also fails it. Geode uses one shared material family and light rig, with quiet native type on each object's stable content frame. The first impression should be sculptural and calm; action areas become energetic only after contact or audio response.

## Visual grammar and material assignments

| Role | Material and geometry | Placement and content rule |
| --- | --- | --- |
| Environment | Tidal deep mineral `#122B3C` to `#081D2A`; softly curved receiving plane, distant abstract mineral/liquid forms | One continuous world behind all five sections. No photographic landscape or text baked into art. |
| Primary gel | `#C6F0F3` body with mint `#A8FFF1` excitation; real 3D thickness, transmission/absorption, clear rim | Play puck, selected destination, key actions. Light penetrates sides and bends through the body. |
| Pearl | `#F2FCFA` opal/ceramic front on a dense underside | List rows, control supports, selected chips. Keep the label area still and sufficiently opaque. |
| Film and water | Lavender `#D0CEF5` interference on limited membranes; refractive water in seek/scene regions | Accent only where the geometry has an optical or physical job. Do not blanket the screen with noise. |
| Dark mineral | `#183947` support above `#081D2A` recess; broad, low-gloss contact shadow | Dock bed, page columns, inspector trays, text backplates. |
| Data typography | Pearl labels, mint active values, pale blue secondary copy | Compose text/icons and real text fields stay level, accessible, and outside vertex deformation. |

The source kit supplies Tidal, Opal, Moss, Obsidian, Aurora, and Amber palettes. Ship Tidal as the coherent default and expose a theme choice only where Geode's existing appearance settings support it. A theme swap changes shared lighting, pigment, absorption, and supports together; isolated recolored buttons look disconnected. Preserve the source's real PBR response rather than using CSS-style alpha as optical thickness.

## Shell, dimensions, and layout hierarchy

Five persistent destinations: **Listen, Library, Visuals, Studio, Settings**. On compact phones the curved D03/UI034 dock sits above the system navigation inset; the five destination lenses have at least 48 dp native hit areas and a clear selected state. A persistent mini player appears above the dock outside Listen. At 840 dp and wider, move the destinations into the left UI038 rail and give Library/Studio a second content pane. A narrow window and a large text scale keep controls in a scrollable column without clipping. Respect status bars, navigation bars, display cutouts, and IME. A landscape phone retains the full route map while the player hero and controls can sit side by side.

Use a 16–24 dp safe page gutter on phones, a 24–32 dp wide gutter, at least 12 dp between adjacent tappable objects, and a bounded readable column for long settings text. These are Android layout targets, not measurements lifted from compressed video pixels. The player gets the largest object (roughly 35–45% of usable portrait height when artwork is present); its play puck remains at least 72 dp visually with a native 48 dp minimum action target. Other screen objects have lower relief so the hero retains hierarchy.

### Compact phone wireframes

```text
LISTEN                              LIBRARY
small location / search             heading / search / import
                                     seven view chips
         album art                   featured album or playlist
      inside C20 ring                floating C02 track rows
                                     list/grid scroll region
track • artist                      mini transport
B07 liquid seek + times             D03 five-destination dock
prev    A22 PLAY    next
Queue     Lyrics     Sleep
Up next preview
D03 five-destination dock

VISUALS                             STUDIO
scene title / launch full view       project header / create
live scene preview / UI071           project cards or empty stage
customize · presets · palette        selected project opens Editor
modulation · shader                  preview + transport
background/layers sheets            timeline lanes + inspector
mini transport + dock               mini transport + dock

SETTINGS
appearance hero / active material
focused category rows
playback · audio · appearance · behavior
folders · external audio · auto · export
help · about
mini transport + dock
```

| Route family | Content order and key library parts | Data and interaction wiring |
| --- | --- | --- |
| Listen root | C20/UI061 ring + real album image inside; track metadata; B07/UI019 seek channel; A22/UI003 play between previous/next; queue/lyrics/sleep shortcuts; next tracks below fold | PlayerViewModel media state drives title, album art, progress, play/pause, next/previous and queue. A missing track shows the UI071 empty stage and explicit import action. Drag changes seek position through the player, not only the mesh. |
| Queue, lyrics, sleep | UI044 sheet for queue/sleep; focused page for timed lyrics | Queue reorder/remove, lyric timing/scroll, timer choices/stop stay reachable by back and assistive tech. Sheet shows the underlying player world through a dimmed receiver. |
| Library root | UI013 search opening shared Search overlay; UI010 chips for tracks, albums, artists, folders, playlists, favorites, recent; featured object; UI057 rows or UI058 grouped cards | Query/import permission is explicit. Rows use C02 dimensional shells with stable artwork/title/artist/actions. Use a virtualized native list; page view state survives section changes. |
| Library details | Album, artist, folder, playlist, smart playlist, duplicate review, track info and tag editor | Detail title and art above tracks. Actions play, add to queue, add to playlist, edit and duplicate resolution call existing repositories. Editable text uses native input with a quiet Opaline shell. |
| Search overlay | UI015 field + suggestions/results, then track/album/artist actions | Launch from any section and close back to its exact prior place. IME and input selection behave normally. |
| Visuals hub | UI071 stage with native visualizer preview; preview controls then Customize, Presets, Modulation, Palette, Shader | Real renderer state and presets drive the preview. A clear full-screen action opens the immersive overlay; no visualizer icon substitutes for Queue. |
| Visuals detail | UI023 B13 dial, UI020 B03 fader, UI025 B21 XY, palette objects, shader text editor; UI044 Background/Layers sheets | Continuous value is immediately reflected in the existing visualizer/scene configuration. Each control has a visible numeric value, stable label and precise native input path. Shader editor remains true editable text. |
| Immersive visualizer | Unobstructed engine-owned render surface with small A03 close/control lenses | Owns rendering focus while open. Back closes it and restores the exact prior Visuals route and shared Opaline world. |
| Studio projects/editor | Projects root: UI058 cards and create project. Editor: preview, transport, timeline lanes, inspector, export | Creating a project saves a real project. Opening an existing project loads its actual tracks/scene. Timeline edits persist. A tablet uses timeline plus inspector; phone inspector is a focused panel/sheet. Export progress/cancel/status are explicit. |
| Settings | UI073 liquid control panel; dense category rows; focused setting pages | B09/B10 toggles, B01/B03 sliders, numeric readouts, permission and storage affordances. A material setting previews its result. Reduced motion is always directly operable. |

### Wide layout and state continuity

The rail owns only section selection. Each section owns a saved stack and scroll/input state. At >=840 dp Library can keep the selected album/playlist detail beside the list; Studio can keep inspector beside timeline; Visuals can keep parameter controls beside preview. Resizing to compact retains the selected route and data. Never depend on screen width to decide the underlying destination. A project edit, active search, unsaved text, playback, and media position must not reset when the viewport changes.

## Navigation and interaction contract

```text
App start/gates → Listen
Dock/rail → section root or that section's saved stack
Repeated selected destination → its root
List or category object → page in the owning section
Queue / Sleep / Background / Layers / Template / Export → contextual sheet
Search / Immersive visualizer → overlay over the invoking route
Android back → dismiss gate → close overlay → close sheet → pop page
             → return to Listen root → system leaves app
Deep link → destination and payload → consume once after handler completes
```

Section changes are a spatial 3D reconfiguration of persistent shells: dock's active lens settles into its socket, the outgoing page retreats, the incoming page moves forward with L01/L02 or L09/L10 where the retained meshes fit. Page pushes and pops use shorter depth travel; sheets rise from their triggering object into UI044. Search opens from its own lens. Predictive back exposes a bounded preview of the previous state and either continues or returns to its exact pose on cancellation. Actual navigation commits immediately to the Navigator; the renderer catches up visually. Gesture cancellation must never leave a pressed object or stale back target.

The route model already exists in `dev.geode.nav`. Keep its data-driven backstack, deep-link, gate and presentation contract. Test root replacement, section switching, saved/restored roots, repeated dock taps, sheet/overlay precedence, predictive-back cancellation, and deep links from a cold start. All five destinations must be discoverable with TalkBack and keyboard navigation in route order.

## Motion and physics score

| Trigger | 3D response | Semantic and lifecycle rule |
| --- | --- | --- |
| First launch | Receiver and dock settle, album ring assembles gently; one authored reveal, not a perpetual spinner | Music and navigation are usable as soon as native state is ready. Skip the reveal on restore/reduced motion. |
| Press / hold | Contact point dents the actual shell; shoulder thickens, rim brightens, support stays seated; hold slowly relaxes | Native action fires on tap without waiting for spring. One pointer owns each contact until up/cancel/disposal. |
| Release / cancel | Damped overshoot and local ripple; shadow and caustic pattern recover with geometry | Cancel leaves the app value unchanged where platform gesture semantics require it and releases force. |
| Seek / fader / dial / XY drag | Thumb stays constrained to a real channel; meniscus and nearby gel follow with lag | Precise value is sent on native drag; on release, engine state and visual position reconcile. |
| Scroll / fling | Floating C02 rows tilt by a few degrees with stagger and spring back; floor contact and occlusion change | List virtualization and fling remain native. Decorative row response does not block scroll or move readable labels. |
| Play/pause | A22 puck compresses, ring receives a short impulse, luminous excitation travels through seek channel | Audio state is source of truth; pause/missing track never fabricates playback. |
| Track change | Album ring turns/retargets and previous color drains as new art arrives | Avoid flashing wrong artwork; reduced motion cross-state changes immediately. |
| Section / page / sheet | L01/L02/L09/L10 motion on shared persistent objects; optional L11 dock growth; real depth and perspective | Retarget from the current pose if interrupted. A sheet preserves its underlying section state. |
| Idle / audio response | Very low amplitude breath; active scene seed/film responds to real audio analysis only when enabled | Stop work offscreen/paused; silence means settled material. Never simulate audio that is absent. |

Use library `MotionController` for local vertex press and springs; use XPBD `SoftBody` for a limited hero or scene specimen where volume/contact simulation is actually bound. `PhysicsWorld` advances at a fixed 1/120 s with bounded substeps. Authored L01–L18 choreography is called a spatial transition, not a fluid mass solver. If moving droplets need a mass claim, wire the library's explicit withdraw/deposit ports and test the ledger. Stable native labels/icons/content frames may follow a rigid parent pose but do not bend with vertices.

## Shared optical world and caustic contract

The screen needs one perspective camera, shared light rig, continuous receiver surface and spatial coordinates for every visible registered object. The host publishes visible Compose bounds, action values and pointer contacts to the local scene; the scene has no authority to mutate player/library/settings state. Controls use A/B/C/D bodies with actual sides and normals. The receiving plane accepts contact shadows and light transmitted/reflected by the same moving objects. Each source mesh used as a refractor must be closed, outward wound and have current transform/material data; receiver UVs must be valid and nonoverlapping.

Use the supplied `RefractiveCaustics` and `attachCaustics` APIs for a **measured subset of active, visible refractors**, starting with the player orb/puck and selected panel. It traces rays through current triangles with Fresnel/refraction/absorption, deposits energy in the receiver UV map, and resets accumulation when geometry/light changes. Static objects can progressively accumulate. Dynamic contacts need a bounded tracing budget and a visible temporary lower-confidence pattern, then converge after motion settles. A photon batch is not a converged image. Prioritize close hero objects and the current section; suspend offscreen meshes and during app pause. The settings can scale quality for flagship devices, but adaptive downshift follows sustained measured frame pressure rather than an arbitrary always-low cap. The supplied volume beam shader is reserved for an unobstructed immersive hero or a correctly depth-clipped pass; it is not stamped onto every control.

Renderer readiness controls native backing opacity: when actual WebGL is ready, front plates use a restrained inset for legibility and reveal the 3D body; while loading/context-lost/unsupported, a complete opaque native control remains usable. A platform dialog or separate window cannot share the world behind the scene unless it hosts its own scene; favor an in-window sheet for Opaline contextual surfaces. The existing engine GL visualizer has explicit ownership in its full-screen overlay. Release meshes, textures, contacts, listeners and renderer resources on removal/lifecycle stop.

## Accessibility and quality gates

Touch targets >=48 dp, keyboard focus visible, stable readable contrast, native text selection/IME, real roles/state announcements, and logical traversal. Reduced motion retains materials, static depth, value feedback and navigation but removes ambient drift, stagger, overshoot, parallax and camera travel. Respect power/thermal pressure, context loss, split screen and app backgrounding. Test a real flagship profile plus a lower capability profile so graceful fallback is proven rather than assumed.

Visual acceptance requires Android captures of all five roots, a list detail, search, a contextual sheet, editor, and immersive visualizer; compare the visible sidewalls/shadow/contact/caustics against the library viewer and video frames. Motion acceptance requires recorded press, drag, scroll, section change, interrupted transition and predictive back; state changes must remain correct. Functional acceptance requires media play/seek/queue, library import/search, visualizer controls, project create/open/edit/export, and settings persistence. Automated gates: navigation model and UI smoke, JS physics/optics contracts, Kotlin formatting/static analysis/unit tests, Android lint, debug assemble, and GitHub Actions results. Record actual FPS/frame time, triangle/texture counts and thermal behavior before asserting a performance tier.

## Current implementation truth

The isolated `codex/opaline-redesign` worktree has a first-pass native shell, restored backend orchestration, creative and library screens, and a local Three.js scene. The second Kotlin compile currently fails on missing `opaline_*` resources and a duplicate `CustomizeSummary`; the first pass is not integration-complete. The scene has shared perspective geometry, material and local deformation/soft-body work, but the causal receiving-plane caustic pass described above has not been integrated or measured. This specification defines the next work, not evidence that those gates have passed.
