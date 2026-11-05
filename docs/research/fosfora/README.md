# fosfora study (2026-11-05)

Seven area reports on [kevinraymond/fosfora](https://github.com/kevinraymond/fosfora) @ `2e85d3b` (fork:
`tessie1993/fosforatess`, indexed in Graphify at the same commit), each set against Geode at `37761e8a`. The plan
that comes out of them is [`../2026-11-05-fosfora-structure-plan.md`](../2026-11-05-fosfora-structure-plan.md).

| Report | Area | Biggest findings for Geode |
|---|---|---|
| [01](01-audio-core.md) | Audio analysis core: hop chain, feature schema, ranging, silence, input hygiene | Wrap-around route smoothing bug; four definitions of silence; no NaN/DC guard; RealFft must land before the hop; keep Geode's consumer-side ranging |
| [02](02-rhythm-structure.md) | Beat, tempo, downbeat, phrase, sections, drops, phase interpolation | Kick + off-beat hat never locks; off-beat bass locks on the half beat; director misses drops and sections of strength 0.4–0.5; false section changes on steady music; visual probably leads the audio |
| [03](03-offline-bench-ci.md) | Offline drivers, golden vectors, accuracy bench, CI | 183 of 352 frame fields depend on fps (measured); export hears up to 1 ms less audio than the host harness; a golden vector is a host contract (FMA); designs for FileDriver, `sound_dump`, the bench and CI |
| [04](04-render-pipeline.md) | Render pipeline, effect model, passes, compositing, safety | 43 shaders apply speed twice; the flash budget never fires and nothing measures the output; slim `Scene`, `Pass` + `FramePlan`, one uniform block |
| [05](05-particles-sim.md) | GPU particles and simulations | Trails dim under the thermal fps cap (source gain not dt-corrected); auto-downgrade never sheds tracers; phone-sized particle ideas only |
| [06](06-modulation-control.md) | Params, bindings, LFOs, presets, transitions, persistence | Base vs modulation split; clamp once; the catalogue has no range table (matrix E0 prerequisite); plan D6 order wrong; export note seed not pinned |
| [07](07-app-ui-product.md) | The app as a product, UI architecture, docs | Renderer ignores the system's "Remove animations"; beat-locked loops; typed command channel to untangle `PlayerSession`; offline help |

Method: each report was written by a separate agent that read the fosfora code (no fosfora code was built or run),
read the matching Geode code, checked fosfora call paths in Graphify, and in some cases ran Geode's own `core/`
against synthetic audio on the host. Claims are cited `file:line`. Measurements came from throwaway harnesses
that were not committed; the recommended tests recreate them. Fosfora is MIT/Apache-2.0: we take ideas and
shapes, not code.
