# core/signal/events

Part of [`core/signal`](../README.md). The moments the engine reacts to, as plain values: raised once a frame, handed out in a fixed order.

| File | Owns |
|---|---|
| `EventKinds.inc` | Every event kind, one row: `GEODE_EVENT(Name, "wire_id")`. Append only: the ordinal is the wire value. |
| `Event.hpp/.cpp` | `Event` (kind, strength 0..1, an extra value, time), `EventFilter` (kinds + minimum strength), `EventQueue` (one frame's events, fixed capacity, no allocation), wire ids. |
| `EventBus.hpp/.cpp` | Subscribe with a filter and a priority; `dispatch` hands each event to every match. Order: events as raised, then priority (high first), then subscription order. Safe to subscribe or unsubscribe from a handler. |
| `SoundEvents.hpp/.cpp` | The detector from the sound frame. Triggers come from a one-row-per-event table (`kick`, `beat`, `drop`…). Derived events have rules: build start and end from the section state, a key change once a new key has been clear for a second, a section change with its id. |

**Adding an event:** add a row to `EventKinds.inc`. Then either add a row to `SoundEvents.cpp`'s trigger table (for a sound-frame trigger), add a rule in `SoundEvents::detect` (for a derived event), or push it from the engine (as with `StyleSwitched`). Nothing else changes: filters, the bus, routes and wire ids all read the table.

**Per frame:**

```
queue.clear()
soundEvents.detect(frame, t, dt, queue)   // + engine pushes: queue.push({StyleSwitched, …})
bus.dispatch(queue)                       // event routes, the director, anything subscribed
```

**Not here:** what an event *does*. That's [`bindings/EventRoutes`](../bindings/README.md) (event → param) or a subscriber's own code.

**Test that proves it:** `core/tests/signal_test.cpp`. It checks:
- the wire ids round-trip
- triggers come in table order with their strength
- BuildStart and BuildEnd fire once each
- a key needs a clear second, and a wobble raises nothing
- a NaN frame raises nothing
- a full queue counts what it drops
- bus order, filters and re-entrancy
