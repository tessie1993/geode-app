# Build, CI, release, tooling

One fact per line. Add a fact as a new bullet in its group.

## Pull request gates

- Don't build locally. Push a branch and open a PR.
- `.github/workflows/build-lint.yml` is the PR gate: `:app:assembleDebug`, the manifest check (`tools/check-manifest.sh`), every module's JVM unit tests, detekt, `:app:lintDebug`, `ktlintCheck`, in that order.
- So a failing unit test hides the lint results behind it.
- The manifest check fails on INTERNET without Cast, on AD_ID, and on the wrong targetSdk, against the debug build.
- `native-host-tests.yml` (the `core/tests` host tests) runs on every PR too.
- `arch-map.yml` runs `tools/layer-gate.sh`, `tools/engine-kotlin-gate.sh`, `tools/no-bpm-bias-gate.sh`, `tools/network-gate.sh` (sockets and WebRTC headers only in `cast/` and `core/cast/`, no other networking submodule) and `tools/actions-pin-gate.sh` (every action pinned to a commit SHA) on every PR.
- `arch-map.yml` also checks that `THIRD_PARTY_NOTICES` equals the shipped `assets/third_party_notices.txt`.
- Read the results with `gh run list --branch <b>` and `gh run view <id> --log-failed`.

## Manual workflows

- `android.yml` (unit tests, `tools/check_shaders.py`, the manifest check, plus an emulator job, `connectedDebugAndroidTest`, API 36) runs by hand from the Actions tab.
- `ui-tour.yml` runs by hand.
- Start one with `gh workflow run <file> --ref <b>`.
- `render-thumbs.yml` and `store-assets.yml` run by hand (see [tests.md](tests.md)).

## ktlint and detekt

- Before you push Kotlin, run `tools/ktlint-changed.sh` (`-F` fixes what it can; base `origin/main`).
- It is CI's `ktlintCheck` without Gradle, in seconds.
- It runs the pinned ktlint 1.5.0 CLI (the engine the Gradle plugin 14.2.0 runs) on the changed files under `.editorconfig`.
- There is no ktlint baseline.
- detekt is configured by `config/detekt/detekt.yml` and baselined per module (`*/detekt-baseline.xml`).
- detekt has no changed-files script yet.
- `.pre-commit-config.yaml` (optional, for `pre-commit install` on a developer machine) runs `tools/ktlint-changed.sh` and the gates of `arch-map.yml` from `tools/`; no Gradle.

## Shipping and versions

- `ship-apk.yml` (pushes to `main`, plus the last job of a green manual `android.yml` run) publishes a debug APK as pre-release N, "BeatRealms 3D <base>.N".
- N = releases so far + 1, reserved as tag `apk-v<base>.N` by `tools/release-number.sh`.
- The version is set only in `gradle.properties` (`geode.versionBase`, `geode.versionCodeBase`).
- The root `build.gradle.kts` makes versionName `<base>.N` and versionCode `codeBase + N` from `-PreleaseNumber=N`, else `<base>-dev`.
- `release.yml` uses the N its commit shipped as.
- `release.yml` builds the Play AAB only on manual runs, signed with the upload key.
- Without the upload key `release.yml` fails unless `allow_unsigned` asks for a dry run.
- `release.yml` also fails unless the commit's checks are green.
- `release.yml` then checks the merged manifest, R8 and the baseline profile.
- `pages.yml` publishes `docs/site/`, `docs/privacy-policy.html` and `docs/help/` (as `/help/`, when that folder exists) to GitHub Pages on pushes to `main`.
- Debug builds sign with the committed `app/debug.keystore` so CI APKs install over each other.

## Store page

- `docs/store/README.md` is the owner's Play Console step list (data safety, permissions, rating, stats, each with code evidence).
- The pasted store text is `fastlane/metadata/android/en-US/`.
- `tools/store-check.sh` checks the store text's lengths.
- `tools/store-stats.sh` recounts the numbers.

## Local setup and agent tooling

- Submodules: `git submodule update --init --recursive`, needed only for native work.
- Your `local.properties` needs `sdk.dir`.
- `tools/agent-setup.sh` installs clangd, glslc and the Android CLI, and the plugins in `.claude/settings.json`.
- `tools/agent-setup.sh` fetches Google's `android/skills` and `chrisbanes/skills` at pinned commits into `.claude/skills/` (git-excluded).
- `skillOverrides` in the settings lists the skills Geode rarely needs by name only, which it cannot do for plugin skills.
- `compile_flags.txt` (with `tools/clangd-stubs`) points clangd at the native core.
- The tracked repo skills are `geode-sound-engine`, `geode-shaders`, `geode-native-core` and Matt Pocock's unpackaged skills.
