# Geode / Opaline redesign

Base: current main at ad9403a, after the previous shell was removed. The PR #76 checkout and its uncommitted experiment remain untouched.

## Reference review

The supplied Opaline 3D Library contains 247 catalogue entries, 157 mesh assemblies, 76 UI recipes, 24 artwork references, seven Cycles references, six themes and executable material, motion, physics and optics modules. The complete first-party documentation, catalogue, source APIs, tests and artwork contact sheets were reviewed before implementation. Vendor Three.js remains an unchanged dependency, not application instructions. The ZIP's handoff documents are technical source material; the user's request defines the scope.

The artwork is the appearance target: rounded dense gel, opalescent shoulders, clear mechanical channels, mineral supports, visible undersides and coherent perspective. The generated artwork and Cycles renders have different fidelity; no mobile performance claim follows from either. A PNG is only a distant backplate. Interactive parts use real geometry.

## Visual system

Tidal is the initial art direction: environment #122B3C, dense gel #C6F0F3, mint excitation #A8FFF1, pearl content #F2FCFA, lavender film #D0CEF5, deep support #081D2A. Large, lightly weighted sans-serif titles frame compact, readable medium-weight controls. Content aligns left, except the player artwork and transport. Labels remain sentence case. Text is native and stays stable while shells respond.

The memorable object is the album's opaline ring; the rest of the screen gives music room to breathe. Depth comes from actual sidewalls and shared lighting. List text sits on quiet mineral fronts, not moving translucent noise. Touch targets are at least 48 dp, list content is virtualized, keyboard focus and screen-reader roles remain native. Reduced motion uses settled surfaces with identical functionality.

## Wireframes

Phone:

    Header / current page             Search
    ┌─────────────────────────────────────┐
    │  Listen: album inside a C20 ring    │
    │  Track title / artist              │
    │  B01 seek channel / time           │
    │       previous  A22 play  next     │
    │  Queue     Lyrics     Sleep        │
    │  Up next / explicit track actions │
    └─────────────────────────────────────┘
    Persistent transport outside Listen
    D03 dock: Listen Library Visuals Studio Settings

Library: seven horizontally scrollable views, a separate import/permission action, then dense native track rows or album/artist groups. A detail route retains its parent's state. Search is an overlay and returns to the invoking section.

Visuals: scene gallery and live preview, then focused Customize, Presets, Modulation, Palette and Shader pages. Background and Layers are contextual sheets. The fullscreen visualizer owns the native GL renderer while visible.

Studio: projects and template entry points, then a workspace with preview, transport, timeline lanes and an inspector. Render progress is a dedicated overlay with cancel and completion states.

Settings: vertical categories followed by focused controls. Continuous values use channel/fader/dial forms; binary values use a pearl in a real track.

Wide windows: navigation moves to a left rail. Content retains a bounded reading width; the library/editor use available space without squeezing the controls. System bars, IME and large fonts must remain clear.

## Runtime contract

One local WebGL2 scene, one camera, one clock and one light rig provide all connected 3D parts. Compose publishes visible component bounds and bounded values. Input and data mutations happen immediately in Android; springs only affect appearance. The supplied contact-deformation controller and a bounded XPBD specimen supply actual motion. Authored transitions are not described as full fluid simulation. Heavy photon tracing and volumetric scattering are not the mobile default.

The scene receives lifecycle and reduced-motion state, caps resolution/work, disposes removed parts, releases cancelled contacts and offers a readable native fallback. Local assets use an intercepted HTTPS origin; no external content or network permission is needed.

## Implementation ownership and checks

Three parallel implementation areas: native navigation/music, 3D runtime, and creative/settings screens. Shared primitives, backend restoration, integration and CI are owned by the primary agent. Backend logic is adapted from the last functioning app without restoring the old visual shell.

Validate navigation stack/back/restore/deep links; JS geometry registration, state, lifecycle and physics invariants; Kotlin formatting/static analysis; unit tests; Android lint; debug APK compilation and assembly. GitHub Actions is the build evidence. Capture visual references from the actual runtime and Android when available; distinguish verified results from remaining device limits.
