---
status: deferred
---

# The pack store: our packs, sold per pack, downloaded from a static catalogue

Until now, Cast to TV has been the app's only internet use. The owner set that rule on 2026-11-02, and CI, the privacy policy and the Data safety form all depend on it. On 2026-11-05 the owner decided, in the 3.0 spec (#455, answer 20 on #433), that **style packs come from an in-app online store**. That is a second internet use. This ADR sets its limits. It amends the Cast-only rule. It does not change how Cast works.

**Deferred (2026-11-05).** The owner shelved the pack store: there are no paid packs for launch, and the store tickets are closed as not planned for now (owner's answer on PR #492). This ADR is kept as the design to start from if the store comes back. Until then, Cast to TV stays the app's only internet use, and nothing below changes any gate, policy or form. The owner's one answer for later: **a refund stops and deletes the pack** (open question 4, as proposed). Hosting, pricing and packs without Pro are dropped for now, and questions 5–7 were not needed.

Nothing in this ADR is built yet. Tracked in #475. Store code (#476) waits for the owner to accept this ADR. The CI gates, the manifest, the privacy policy and the Data safety form change in the same PR as the store code, not before. § CI gates and § Privacy policy and Data safety list exactly what each will change.

## Decision

1. **We sell only our own packs.** A pack holds environments, generators or models (GLOSSARY: Scene, generator). There are no user-published or community packs and no accounts (#455 Out of scope).
2. **Each pack is its own Play Billing in-app product.** It is non-consumable and is not part of Pro. Pro, the one-hour trial and `FreeTier` do not unlock packs. A pack does not unlock Pro.
3. **The catalogue is a set of static files we host.** The app only downloads. It sends no account, identifier, cookie, analytics or crash report, and it does nothing in the background. The app contacts the host only when the user opens the store, buys a pack, installs one, restores purchases or updates a pack.
4. **Every download is checked before import.** The signature over the pack's manifest must verify, every file must match its hash in the manifest, and the user must own the pack. Then the pack goes through the same C++ import limits as the user's own content. Being signed lets a pack skip no limit.
5. **Networking lives in one Kotlin package, `dev.geode.store`,** just as Cast's lives in `dev.geode.cast`. It is an HTTPS client to one host and nothing else. No socket, no server, no cleartext.
6. **The logic is C++, in a new `core/pack`, behind `geode_api.h`.** That covers parsing the catalogue and manifest, checking signatures and hashes, unpacking, choosing versions and sending each file to its importer. Kotlin fetches bytes, calls Play Billing, writes files and draws the store UI. This is ADR 0003's owner rule. It amends #455's testing line "JVM tests for the pack catalogue parse and signature check": those tests become C++ host tests (`core/tests/pack_test.cpp`).

## Why not offline import or Play Asset Delivery

The owner chose an online store. These alternatives were weighed and turned down:

- **Offline import (the user picks a `.geodepack` file).** It keeps the Cast-only rule and needs no hosting. But Play Billing has nothing to sell: the app would have to sell a licence and then make the user find and fetch a file somewhere else. The file can also be shared, so the signature would be the only lock. Users still get their own models through glTF import (#474). That path is separate and stays offline.
- **Play Asset Delivery (on-demand asset packs).** Again no hosting and no INTERNET claim of our own. But every pack has to ship inside an app bundle. A new pack or a fixed pack then waits for an app release and its review. Asset packs also have size caps, per pack and per app. Play delivers an on-demand pack to any install that asks for it, whether or not the user bought it, so entitlement would still be checked only on the device. And the catalogue could never be larger than one bundle.
- **Our own server with accounts or licence checks.** The owner ruled out accounts. It would also mean holding personal data, which the privacy policy promises we never do.

A static catalogue keeps the store's internet use as small as it can be: anonymous HTTPS GETs of public files.

## Threat model

| Threat | Defence |
|---|---|
| A pack is tampered with on the host, a CDN or the network | HTTPS, plus an Ed25519 signature over the manifest by a key the app pins. The manifest lists each file's SHA-256 and size. One mismatch rejects the whole pack, and nothing is written to `files/packs/`. The private key stays offline, never in CI or on the host. |
| A key is compromised | The app ships a short list of trusted public keys. A new key arrives with an app update, and an update can drop the old key. The catalogue is signed the same way, so a forged catalogue cannot point at forged packs. |
| An older pack is replayed (downgrade) | The manifest's `version` must be higher than the version installed. The catalogue's version for a pack has to match its manifest. |
| A malicious or broken glTF | Pack models go through the #474 importer (cgltf, meshoptimizer, Basis) and #455's limit table, with the same refusals as a user model: malformed file, unknown required extension, still over the limits. That importer gets fuzzed in host tests. |
| A malicious or heavy shader | A generator's GLSL may `//#include` only the `lib_*` files allow-listed in `core/viz/ShaderIncludes.inc`. A pack cannot add a lib, and it cannot override one. Our pack pipeline runs `tools/check_shaders.py` and `tools/glslcheck` stills (#470: no black frames, no blown highlights, no flash-limit breach) before signing. At runtime, a shader that fails to compile falls back like any style, and `FlashLimiter` still holds every frame (WCAG). Frame cost is checked on our side, against the `DEVICE_CHECKS.md` budget. |
| A zip-slip path or a decompression bomb | `core/pack` unpacks only the entries the manifest lists. Paths are flat and from an allowed character set, with no `..`, no absolute paths and no links. The manifest caps each entry's size and the pack's total size. Unpacking stops at the declared size. |
| A pack is used without being bought | The same strength as Pro: an on-device check of Play's signed purchase (`billing/PurchaseVerifier`). Pack files are public on the host, so anyone with the URL can download them, and that is accepted. A pack's content loads only while `PackEntitlements` says the user owns it. |
| The store leaks privacy data | The host sees the IP address, a fixed user agent and which files are fetched. No identifier, cookie or query string is sent. We pick a host whose access logs can be turned off (open question 1). No fetch happens at launch or in the background. |
| Play policy on downloaded code | Packs hold data only: glTF, textures, GLSL that our engine compiles, and manifests. They hold no dex, no native code and no script. Shaders are what games commonly download, but the submission notes still state this outright. |

## Pack format (sketch)

`<packId>-<version>.geodepack` is a zip with flat entries:

- `manifest.txt`: a line-based text format parsed in C++, like `geode_catalogue_text`. It is not JSON: `org.json` is a stub in local tests, and parsing is engine logic. Its fields:
  - `format` (the pack format version)
  - `id`
  - `version` (an integer)
  - `product` (the Play product id, `pack_<id>`)
  - `min_engine` (the `geode_api` level the pack needs; an older app refuses the pack and says so)
  - `kind` rows (environment, generator or model, each with its id and entry file)
  - `file` rows (path, size, SHA-256)
  - `licence` rows (file → licence and attribution, for third-party assets)
- `manifest.sig`: the Ed25519 signature over `manifest.txt` and its key id.
- `LICENCES.txt`: shown in the store and in Settings' open-source notices.
- `models/*.glb`, `generators/*.frag`, `environments/*`, `thumbs/*.webp`.

The catalogue is `catalogue.txt` plus `catalogue.sig`, in the same format. It has one row per pack: id, product id, version, size, the pack's SHA-256, its URL path, the thumbnail's path, `min_engine`, and a state, `listed` or `withdrawn`.

Installed packs go in `files/packs/<id>/<version>/`. They are left out of cloud backup and device transfer, like `project-takes` (#384), because an owned pack can be downloaded again. Restore downloads them on demand.

Ed25519 checking in C++ needs a small vendored library, such as Monocypher (CC0 or BSD-2) or libsodium's verify path, recorded in the provenance manifest. Android's platform Ed25519 starts only at API 33, and minSdk is 26.

## Pro and billing

- `BillingRepository` (`pro/`) has one product, `muzviz_pro`. It stays as it is. A new pure interface, `PackEntitlements` in `pro/`, answers `owned(packId)`, the price of each product and a purchase flow per product. `billing/` implements it on the same `BillingClient`.
- `PlayBillingRepository.queryOwned` already asks Play for every `INAPP` purchase and then filters to `PRO_PRODUCT_ID`. Pack purchases appear in that same answer, routed by the `pack_` prefix, so the store adds no second connection to Play. A pack is verified, acknowledged (the three-day refund rule applies) and cached with its signed purchase per product. Revoking follows `EntitlementStore`'s rule: only a successful full list that no longer has the product revokes it. An error never does.
- `FakeBillingRepository` gets packs for debug builds and tests. `GetFeatureGateUseCase` does not change: packs are not a `ProFeature`.
- `docs/play/BILLING.md` gets a pack product section: id scheme, price tier and test steps. Product ids can never be reused, so the `pack_<id>` names are fixed once created.

## CI gates (what will change, in the store PR, not this one)

| Where | Today | After |
|---|---|---|
| `android.yml`, step "Check that only Cast uses the internet" (line 279) | Network-API grep over `app/src/main` and `engine/*/src/main` minus `/dev/geode/cast/` | The step is renamed "Check that only Cast and the pack store use the internet". The exclusion becomes `grep -vE '/dev/geode/(cast\|store)/'`. A second grep allows only `javax.net.ssl.HttpsURLConnection` and `java.net.URL` in `dev/geode/store/`: any `Socket`, `ServerSocket`, `DatagramSocket` or plain `HttpURLConnection` cast fails, and so does any `http://` literal. A third grep requires that the only host literal in `dev/geode/store/` is the one constant `StoreEndpoint.HOST`. `android.webkit.WebView` and `android.app.DownloadManager` join the banned pattern app-wide, because both reach the network without a socket API the grep can see. The error messages name both packages. |
| Same step, SDK ban list (line 292) | `okhttp\|retrofit\|ktor\|cronet\|volley\|firebase-…\|…` | Unchanged. The store uses the platform's `HttpsURLConnection`, so no new SDK joins. Only the message changes. |
| `android.yml`, "Check the merged manifest" (line 305), and `release.yml` (line 135) | INTERNET only alongside `dev.geode.cast.CastService` | INTERNET only alongside `CastService` or `dev.geode.store.PackDownloadService`, the store's foreground service (`dataSync`, a type already declared), so that one leaving does not hide the other. Both also fail on `usesCleartextTraffic="true"`. AD_ID and BILLING checks are unchanged. |
| `android.yml`, native host tests | None for packs | `core/tests/pack_test.cpp`, wired like the other `core/tests` targets: catalogue and manifest parse, a bad signature, an unknown key, a hash mismatch, a downgrade, zip-slip, a size bomb, a too-new `min_engine`, a withdrawn pack. |
| `tools/engine-kotlin-gate.sh` | `engine/*/src/main/`, `export/`, `audio/dsp/` | Pack import is added to the areas #461 extends. `dev.geode.store` holds glue only, and parsing or checking there fails review. |
| Manifest | INTERNET and ACCESS_NETWORK_STATE, for Cast | The permissions don't change. Adds `PackDownloadService` (`foregroundServiceType="dataSync"`). No `networkSecurityConfig` is needed: with targetSdk 36, cleartext is already off by default, and the new gate keeps it off. |

## Privacy policy and Data safety (what will change)

Both `docs/privacy-policy.html` and `docs/privacy-policy.md` change:

- **The short version (line 28).** "Collects nothing, sells nothing and has no servers. Two exceptions" becomes three exceptions: Pro and **packs** are bought through Google Play, Cast to TV stays, and the **pack store** downloads from our static file host. The text says the host receives the request and nothing else.
- **"Does not collect" list (line 36).** "No internet use except Cast to TV" becomes "except Cast to TV and the pack store, which only downloads".
- **New section, "Pack store".** It says the store contacts the host only when the user opens the store, installs, updates or restores a pack. It says the host sees the IP address and which files are fetched, and nothing else: no account, identifier, cookie or analytics. It states the host's log policy (open question 1). It says downloads are checked on the device and stored in the app's private storage.
- **"Buying MuzViz Pro" (line 58).** This becomes "Buying MuzViz Pro and packs". It keeps the same Play exchange and the same on-device verification and cache.
- **Permissions (line 73).** INTERNET and ACCESS_NETWORK_STATE: "for Cast to TV and the pack store's downloads".
- **Third-party components (line 99).** Adds the vendored signature library. "No servers" becomes "no servers that receive your data".

`docs/play/DATA_SAFETY.md`:

- The facts at the top change from "Cast is the only internet use" to Cast plus the store's anonymous downloads.
- **Financial info › Purchase history.** Still collected and not shared, but its note now covers pack purchases too. Required or optional stays **Optional**.
- **"Encrypted in transit".** Still **Yes**: the store is HTTPS only, which the gate enforces.
- **New data types.** If the host keeps no access logs, **none**: a download of public files that we never record is not collection, and the IP address is not a declared type when nothing is derived from it. If the host keeps logs, we declare **App activity › Other actions** (which packs were fetched), **Optional**, for **App functionality**. The owner decides (open question 1).
- **Audit notes.** The `play-policy-insights` audit is re-run, as the file itself requires whenever "anything but Cast uses the internet".
- **Other Play files.** `docs/play/CONSOLE_CHECKLIST.md` gets the pack products. `CONTENT_RATING.md` already answers "digital purchases: yes", so check it, though no change is expected. `STORE_LISTING.md` says "In-app purchases" already, and its description adds packs.

## Consequences

- "Cast to TV is the only internet use" stops being true wherever it is written: AGENTS.md › Conventions, both workflows, both privacy policies and `DATA_SAFETY.md`. All of them change in the store PR.
- AGENTS.md gains a `store` row in the `:app` package table, `core/pack` in the native map, `PackDownloadService` under Entry points and `pack_test.cpp` under tests.
- Without a server, a pending pack purchase that clears while the app stays closed for three days is refunded by Play. This is Pro's existing limit.
- The pack pipeline (packing, signing, check_shaders, glslcheck stills, upload) lives outside the app. It is a separate repo or a manual workflow, never with the signing key in CI.

## Open questions for the owner

1. **Hosting.** Which host serves the files? GitHub Pages is free but can't turn off access logs and caps bandwidth. Cloudflare R2 or Pages lets logs be turned off. A plain CDN bucket is another option. The answer decides the Data safety row above and the policy's log sentence.
2. **Pricing.** What is the price tier per pack (one tier, or by size)? Is there a bundle? A bundle is one more product id that grants several packs.
3. **Free users.** Does a bought pack work without Pro (proposed: yes, since it is "not included in Pro")? Can a user try a pack's generator live while the purchase sheet is open, like Pro styles (`PlayerSession.leaveScenePreview`)?
4. **Refunds.** *Answered (2026-11-05): stop and delete, as proposed.* A refund revokes on Play's next full list. Proposed: the pack's content stops loading, and its files are deleted on the next store open. A Scene or preset that used it falls back to the default generator and says why.
5. **Withdrawn packs.** When the catalogue drops a pack, proposed: owners keep it installed and working forever. The catalogue keeps a `withdrawn` row, hidden from browsing, so owners can still restore it after a reinstall. The Play product is deactivated, not deleted: Play still returns owned purchases for inactive products. Does the owner commit to hosting withdrawn packs indefinitely?
6. **Updates.** Should a new pack version install automatically on the next store open (proposed), or wait for the user to tap it? The old version is kept until the new one verifies.
7. **Signing key custody.** Who holds the offline Ed25519 key, and where is its backup kept?
