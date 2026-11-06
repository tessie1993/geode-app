# core/signal/bindings

Part of [`core/signal`](../README.md).

**Here now:** `PlayingRoutes` (#352), the routes a renderer plays for the style it has asked for: the style's DNA (`viz::buildStyleDna` at the params' `reactSeed`), merged with the user's list under the frame's hand mask (`viz::mergeHand`). The renderer calls it at its frame latch, after `StyleSchedule` has applied this frame's cue, so a take's switch or a beat cut plays the new style's DNA from its first frame; the shell only hands over the user's list (`geode_viz_set_user_reactions`). Tested by `core/tests/playing_routes_test.cpp`. It is the first piece of the bus below.

**Built: `EventRoutes`** (`EventRoutes.hpp/.cpp`). An event route is a reaction route whose source is an event (GLOSSARY): *when* (an `EventFilter`), *every Nth*, a target param, and one action. It is the native home for Customize › Music's moments → actions and for the event recipes, which are bundles of event routes.
- **Actions that change the base** go through the param state machine with origin `Engine`, so a finger or a timeline still wins and a preset's glide is not torn: Set, Nudge (round the circle for a hue), GlideTo, Toggle, Step (wrapping, for a palette or a fold count).
- **Pulse** adds a decaying modulation, scaled by the event's strength.
- **The flash limit:** a pulse on a `luminance` param decays no faster than 0.35 s and starts at most three times a second per param (WCAG 2.3.1), whatever the event rate. `safety::apply` still clamps the level after.
- **A new clock:** engine time is not monotonic (a new render, a seek, an export restart). A pulse earlier than its param's last pulse drops that param's live pulses and flash history and starts its clock again, so the limit holds within the new clock.
- It hears events from an [`events/EventBus`](../events/README.md). That bus hands out moments; the route bus below evaluates continuous routes each frame. They are two different things.

**Still to build:** the continuous routes below.

**Owns:** `Route` and `Bus`.
- `Route { owner, source id, transforms[], target param id, weight, combine }`. Owner is one of: user, style DNA, director, macro, note response. Combine is add, multiply or lerp-to.
- Transforms, applied in order: range map, curve, attack/release, quantise, invert, scale (fosfora `bindings/transforms.rs`).
- `Bus::evaluate(sources, store)`: one place decides which owner's routes play on which moment. Today that rule is split across `hand::` in `core/viz/Reactions.cpp`, `mergeHand` in `ReactionDna.cpp` and the director dropping routes on user-owned sources (`Director.cpp`, `scaled(..., userOwned)`). It becomes one function with a test.

**Moves in (as of `cf7e88f`)**
- `core/viz/Reactions` (`ReactRoute`, `ReactionEngine`; four engines each re-step the same `ReactSignals` table today) becomes one bus with one source snapshot
- `core/viz/ReactionDna` (stays a route producer: a style's default routes)
- the hand mask, from a process-wide atomic (`hand::gMask`) into per-renderer bus state
- `LfoTarget` ordinals map to param ids for old saves only

**fosfora:** `bindings/types.rs` (`Binding`, `BindingTarget`, `TransformDef`), `bus.rs`, `apply.rs` (normalise → scale into range → clamp), `catalog.rs` (target lists derived, so they cannot drift). We do not copy last-binding-wins.

**Test that proves it:** with a user route, the style DNA and the director all on the same source, the bus plays exactly the routes the ownership rule says, at every frame rate; export and live give the same param values for the same audio.
