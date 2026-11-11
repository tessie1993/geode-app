# Screenshots, feature graphic and icon

Play's rules are from "Add preview assets to showcase your app"
([answer/9866151](https://support.google.com/googleplay/android-developer/answer/9866151), read
2026-11-11):

| Asset | Rule |
|---|---|
| Screenshots | JPEG or 24-bit PNG, no alpha. Each side 320 to 3840 px, the long side at most twice the short side. At least 2 to publish, up to 8 per device type |
| Large screens (7-inch, 10-inch tablet) | 16:9 or 9:16, each side 1080 to 7680 px, at least 4 |
| To be eligible for recommendations | At least 4 screenshots of 1080 px or more: 1920 x 1080 landscape or 1080 x 1920 portrait |
| Content | The real app. A tagline may cover at most 20 % of the image. No device frames needed, no "best", "#1", "new", prices or calls to action. A clean status bar. Alt text up to 140 characters |
| Feature graphic | 1024 x 500, JPEG or 24-bit PNG, no alpha. Required |
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
| 3 | `03-visual-nebula` | Nebula, full screen | A glowing nebula that pulses with the music |
| 4 | `04-customize` | Customize's nine groups | The Customize menu: quick knobs, look, shape, motion, camera, music, effects, liquid |
| 5 | `05-visual-terrain` | Terrain, full screen | A mountain ridge flown over as the music plays |
| 6 | `06-player` | The player with a track playing | The music player with a track playing and its controls |
| 7 | `07-studio-render` | Studio › Render | The video render page: source, look, format, length and save |
| 8 | `08-presets` | The preset grid | A grid of saved looks to apply to the visual |

They show the **free** app (owner decision, 2026-11-11: "screenshots free, record pro"): every
style on screen is one a free user keeps, and Pro badges in the lists are what a free user
sees. `StoreSetup` refuses a Pro style in a screenshot test, and no workflow input can turn a
screenshot into a Pro one. The recorded clips are the Pro half ([promo-video.md](promo-video.md)).

### Honest limits

- **Not run yet.** There is no emulator in the agent container, and the workflow was not
  triggered. The first run is the test of it. Things that run could show: a label the test
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

Play requires one. `docs/brand/muzviz-feature-graphic-1024x500.png` is the right size and
format but carries the old "MuzViz" wordmark, so it cannot ship under the new name.

What the workflow gives: `feature-graphic/<style>-1024x500.png`, six frames of free styles drawn
by the app's own renderer at 2048 x 1000 and filtered down
(`app/src/androidTest/java/dev/geode/store/StoreFramesTest.kt`, the same offscreen path that
renders the style thumbnails). They are renders, not screenshots: there is no app chrome
because a feature graphic has none. They are valid feature graphics as they are.

What only the owner can decide: whether the graphic carries the wordmark. If it does, it is
the "BeatRealms 3D" lockup from the rename's brand work laid over one of these frames by the
brand tooling (`docs/brand/README.md`: `tools/brand_assets.py`, the outlined Montserrat
wordmark). Keep the wordmark inside the middle 60 % (Play crops the edges in some placements),
no tagline, no "new", no price, no device picture.

## App icon

`docs/brand/muzviz-icon-512.png` is 512 x 512, RGBA PNG, 358 KB: it passes Play's format rules
(checked with Pillow). It is the neon eye mark with no wordmark, so the rename does not touch
it unless the owner wants a new mark. Play applies its own rounded mask and shadow; do not
bake either in.
