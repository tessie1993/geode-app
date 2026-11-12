## What and why

<!-- What changes for the user or the code, and why it was broken or missing. Issues: #… -->

## Owner's rules (AGENTS.md)

- [ ] Kotlin stays the main-thread shell; any new logic is in `core/` with a host test in `core/tests/`
- [ ] Engine Kotlin only shrinks (`tools/engine-kotlin-gate.sh`); nothing outside `ui/` and `di/` names `dev.geode.ui`
- [ ] No tempo assumed, preferred or defaulted (`tools/no-bpm-bias-gate.sh`)
- [ ] A new prefs key or file is declared in `PrefsSchemaTest`; a new action is pinned to a commit SHA
- [ ] AGENTS.md tables updated where a module, package, entry point or thread changed

## Checked

<!-- What CI covers, and what it cannot: a short device checklist for anything that needs a phone. -->
