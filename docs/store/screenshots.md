# Screenshots, feature graphic and icon

Play's rules are from "Add preview assets to showcase your app"
([answer/9866151](https://support.google.com/googleplay/android-developer/answer/9866151), read
2026-11-11):

| Asset | Rule |
|---|---|
| Screenshots | JPEG or 24-bit PNG, no alpha. Each side 320 to 3840 px, the long side at most twice the short side. At least 2 to publish, up to 8 per device type |
| Large screens (7-inch, 10-inch tablet, Chromebook) | 16:9 or 9:16, each side 1080 to 7680 px, at least 4 per type uploaded. Optional, but without them the app is not featured on large screens |
| To be eligible for recommendations | At least 4 screenshots of 1080 px or more: 1920 x 1080 landscape or 1080 x 1920 portrait |
| Content | The real app. A tagline may cover at most 20 % of the image. No device frames needed, no "best", "#1", "new", prices or calls to action. A clean status bar. Alt text up to 140 characters |
| Feature graphic | 1024 x 500, JPEG or 24-bit PNG, no alpha. Required. Key elements away from the edges; avoid a pure black, white or dark grey ground; no device pictures, badges, price or "best". Alt text up to 140 characters |
| App icon | 512 x 512, 32-bit PNG with alpha, at most 1024 KB |

## How the screenshots are made

No screenshot is drawn by hand. The manual workflow **Store assets**
(`.github/workflows/store-assets.yml`; Actions tab › Store assets › Run workflow, or
`gh workflow run store-assets.yml --ref <branch>`) boots the CI emulator, runs the real app with
a real style reacting to music that is really playing, and uploads the artifact
`store-assets-<run>`:

```
screenshots/phone/        8 x 1080 x 1920   portrait
screenshots/phone-land/   8 x 1920 x 1080   landscape (the visual at its best)
screenshots/tablet7/      8 x 1920 x 1080   landscape, 600 dp wide
screenshots/tablet10/     8 x 2560 x 1440   landscape, 720 dp wide
feature-graphic/          <style>-1024x500.png, and the 2048 x 1000 frame it was cut from
promo-clips/              raw screen recordings (see promo-video.md)
report.md                 every file's size against the rules above; problems.txt
```

The pieces:

- `app/src/androidTest/java/dev/geode/store/StoreShotsTest.kt`: one test per screenshot,
  skipped unless `-e storeshots 1`. `StoreSetup.kt` seeds the app through its own stores (past
  onboarding with the `publishSeededGuiPrefs` hand-over from PR #1007, the style on screen,
  Settings › Performance) and plays `DemoGroove`, a drum, bass and pad loop generated in code,
  through the real `PlaybackService`. The screenshot is the system compositor's, so it holds
  the GL surface.
- The workflow cuts each screen from one 2560 x 1600 emulator with `wm size` and `wm density`,
  because Play wants 16:9 for tablets and no stock device profile is 16:9, and because today's
  tall phone profiles break the "long side at most twice the short side" rule.
- `tools/store_assets_collect.py` strips alpha, checks every size and fails the run when a set
  is short or the wrong size.

The eight, in upload order:

| # | File | Shows | Alt text (under 140 characters) |
|---|---|---|---|
| 1 | `01-visual-neon-chrome-sea` | The app's first style, full screen | A chrome sea under neon light, moving with the music |
| 2 | `02-styles` | The Styles menu over the running visual | The list of visual styles, opened over a nebula that reacts to the song |
| 3 | `03-visual-nebula` | Nebula, full screen | A glowing nebula that moves with the music |
| 4 | `04-customize` | Customize's groups (ten on Terrain; Engine shows only on styles with an engine of their own) | The Customize menu: quick knobs, look, shape, motion, dive, camera, music, effects, liquid, expert |
| 5 | `05-visual-terrain` | Terrain, full screen | A mountain ridge flown over as the music plays |
| 6 | `06-player` | The player with a track playing | The music player with a track playing and its controls |
| 7 | `07-studio-render` | Studio › Render | The video render page: source, look, format, length and save |
| 8 | `08-presets` | The Presets list (a text list, no previews) | A list of saved looks to apply to the visual |

They show the **free** app (owner decision, 2026-11-11: "screenshots free, record pro"): every
style on screen is one a free user keeps, and Pro badges in the lists are what a free user
sees. `StoreSetup` refuses a Pro style in a screenshot test, and no workflow input can turn a
screenshot into a Pro one. The recorded clips are the Pro half ([promo-video.md](promo-video.md)).

### Honest limits

- **First run, 2026-11-11 (run 43112454833): incomplete.** Between 0 and 5 of the eight per set:
  `UiAutomation.takeScreenshot()` returned null on some captures with the visual on screen
  (`StoreStage.shot`), the emulator went offline in the 10-inch set, and the collect step
  failed to install Pillow, so no report was written (the Pillow pin is fixed). Other things a
  run could show: a label the test
  taps has changed; `wm size` on this emulator image gives screenshots of another size (the
  collect step will say so); the app dies at High quality (the workflow then retries at
  Balanced and Saver and records which mode each set used in `report.md`).
- **Software rendering.** The emulator has no GPU. The frame in a screenshot is the real
  renderer's, but at the render scale of the Performance mode that survived.
- **The demo track.** The player shows "Demo groove" with generated cover art. For a player
  screenshot with a real cover, take that one on a phone with music the owner has the rights
  to show.
- **Style choice.** The three full-screen styles (Neon Chrome Sea, Nebula Dust, Terra Ridge)
  were picked from the free set without seeing them rendered. Change the `styleId` on the
  `@StoreShot` in `StoreShotsTest.kt` after looking at the first run.

If the owner prefers real-phone screenshots: Play accepts them as long as the sizes above hold;
crop to 9:16 or 16:9 and export without alpha.

## Feature graphic

**Ready:** `docs/brand/beatrealms-feature-graphic-1024x500.png` (1024 x 500, RGB PNG, no alpha),
made from `docs/brand/beatrealms-feature-graphic.svg` by `tools/feature_graphic.py` and rendered
as `docs/brand/README.md` › Regenerating says. It is the launcher icon's neon eye beside the
app's own wordmark (the glass script "BeatRealms" over a chrome "3d", read from
`res/drawable/splash_wordmark.xml`, so it follows the app) over an indigo ground, everything
inside the middle 80 %, no tagline, no screenshot. Regenerated 2026-11-11: the first version
still carried the wordmark from before #1047. Alt text: "The BeatRealms 3D eye logo, made of sound waves,
over a spectrum of light".

The ground is indigo on purpose: Play asks feature graphics to avoid pure black and dark grey,
and the app's own black would vanish into the store's dark theme. If a promo video is added,
Play draws a play button in the centre of this graphic; it lands between the eye and the
wordmark, on the first letter's edge, which is acceptable.

The **Store assets** workflow also writes `feature-graphic/<style>-1024x500.png`, frames of
free styles from the app's own renderer (`StoreFramesTest.kt`). They are valid alternatives
without the wordmark.

## App icon

**Ready:** `docs/brand/beatrealms-icon-512.png` is 512 x 512, RGBA PNG, 359 KB: it passes
Play's format rules (checked with Pillow). It is the neon eye mark the launcher icon uses, with
no wordmark (`tools/brand_raster.sh` makes it from `docs/brand/flow/icon-neon.jpg`). Play applies its own rounded mask and shadow; do not
bake either in.

## Shot list (summary)

| Set | Count | Size | Source | Status |
|---|---|---|---|---|
| Phone | 8 (min 2; 4+ at 1080 px for recommendations) | 1080 x 1920 portrait | Store assets workflow, `screenshots/phone/` | Needs an emulator run (not possible in the agent container) |
| Phone landscape (optional extra) | up to the 8 phone slots | 1920 x 1080 | `screenshots/phone-land/` | Needs an emulator run |
| 7-inch tablet | 8 (min 4) | 1920 x 1080 | `screenshots/tablet7/` | Needs an emulator run |
| 10-inch tablet | 8 (min 4) | 2560 x 1440 | `screenshots/tablet10/` | Needs an emulator run |
| Chromebook | optional | 16:9 | not made | Skip unless the owner wants it |
| Feature graphic | 1 | 1024 x 500 | `docs/brand/beatrealms-feature-graphic-1024x500.png` | Ready |
| App icon | 1 | 512 x 512 | `docs/brand/beatrealms-icon-512.png` | Ready |
| Promo video | optional | YouTube URL | [promo-video.md](promo-video.md) | Owner |

Every screenshot shows a free style and real app screens, no device frame, no tagline, a clean
status bar, and an alt text from the table above.
