# core/signal/bindings (skeleton, empty)

Part of [`core/signal`](../README.md). Nothing here yet.

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
