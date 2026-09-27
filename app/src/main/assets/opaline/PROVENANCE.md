# Geode Opaline adapter

The visual source is the user-supplied `Opaline-3D-Library.zip`, library version 3.0.0.
The original catalogue, all 76 compositions, implementation status, artwork and API
documentation were inspected when selecting the interface parts.

Included original source: geometry, physical surface shaders/materials, local
contact motion, spatial transitions, fixed-clock physics, XPBD soft body and
particles. `runtime.js` and `protocol.js` are Geode's new integration code.
Three.js 0.186.1 is bundled, including the required MarchingCubes and
RoomEnvironment modules. Its MIT notice is retained at `vendor/three/LICENSE`.
`artwork/background-tidal.png` is the original generated distant appearance plate.
It is never used as control geometry, a normal map, or proof of solver behavior.

The host uses a single perspective camera, shared environment lighting and one
fixed simulation clock. Native controls retain semantic actions, keyboard input,
accessibility, navigation and list virtualization. Transmitted native rectangles
place actual independent library meshes behind the corresponding stable content.

UI mapping: A01/A03/A05/A22 actions, C01/C03/C04 panels, C20 media frame, D03 dock,
B01/B07 sliders, B03 faders, B09 toggle, B13 dial and B21 XY control. These correspond
to the library's UI001/UI003/UI008/UI019/UI020/UI023/UI025/UI034/UI039/UI041/UI061 roles.

Press deformation and slider spring motion are the library's authored geometric
interaction controller. The separate, bounded sculpture uses actual tetrahedral
XPBD edge/volume constraints; floaters integrate acceleration and drag. This
adapter does not claim topology-changing free fluid, full volumetric photon
caustics, foam topology or offline Cycles render quality at mobile frame rates.

Performance bounds: at most 96 visible registered parts, 48 instanced floaters,
a 6 × 4 × 6 tetrahedral lattice with eight XPBD iterations, 1/120 second fixed
steps with at most four steps per rendered frame. DPR starts at at most 2 and
reduces in 0.2 steps to 0.85 under sustained frame pressure. One prioritized
visible A22, B07, C01 or C03 shell drives a 96 × 96 UV receiver caustic map.
The CPU photon tracer runs every third rendered frame, with a measured 1–4 ms
slice and at most 8 photons per moving update or 32 per settled update. This is
a progressive low-sample estimate, not a converged solution on every frame.
Rendering pauses on lifecycle/background/hidden state. Reduced motion settles
input geometry and renders only changed state or progressive caustic sampling.
Native gradient surfaces remain available when WebGL is loading or unavailable.
No remote assets, network fallback or privileged JavaScript-to-Java bridge exist.

Run `node --test tools/opaline/*.test.mjs` from the repository root for adapter
projection, finite geometry, live contact, cancellation, value timing and bounded
physics checks. Real-device frame timing remains a separate validation gate.
