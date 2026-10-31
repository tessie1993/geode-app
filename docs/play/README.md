# Publishing MuzViz Studio Pro on Google Play

Start with **[CONSOLE_CHECKLIST.md](CONSOLE_CHECKLIST.md)**: the step-by-step list from
creating the developer account to a staged production rollout. The other files are the
answers it points to.

| File | What it's for |
|---|---|
| [CONSOLE_CHECKLIST.md](CONSOLE_CHECKLIST.md) | Every console step, in order, with the GitHub secrets the release workflow needs |
| [BILLING.md](BILLING.md) | The `muzviz_pro` one-time product, how the app verifies and restores it, how to test it |
| [STORE_LISTING.md](STORE_LISTING.md) | Title (≤30), short (≤80) and full description, category, tags |
| [GRAPHICS.md](GRAPHICS.md) | Icon, feature graphic spec, the screenshot list |
| [DATA_SAFETY.md](DATA_SAFETY.md) | Data safety form answers, derived from the merged manifest and the code |
| [POLICY_AUDIT.md](POLICY_AUDIT.md) | Automated Play policy audit of the code (play-policy-insights, 2026-10-30): compliant, two suggestions |
| [RELEASE_READINESS.md](RELEASE_READINESS.md) | What still blocks a production release, ranked, plus the manifest and intent security pass |
| [R8_KEEP_RULES.md](R8_KEEP_RULES.md) | R8 keep-rule audit of the release build (r8-analyzer, 2026-10-30): four rules to drop or narrow, two duplicates |
| [CONTENT_RATING.md](CONTENT_RATING.md) | IARC questionnaire answers, target audience, photosensitivity warning |
| [../privacy-policy.md](../privacy-policy.md) / [../privacy-policy.html](../privacy-policy.html) | The privacy policy to host at a public URL |

Build: `.github/workflows/release.yml` produces the AAB (signed when the upload-key
secrets exist) on every pull request and on `v*` tags.
