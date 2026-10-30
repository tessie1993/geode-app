# Store graphics

Specs are Play Console's current requirements. Every image: no device frames required,
no "best"/"#1" text, no prices or "free" badges, no Google Play or Android logos.

## App icon

- 512 × 512 px, 32-bit PNG with alpha, max 1 MB.
- Export from the adaptive launcher icon (`app/src/main/res/mipmap-*`), full bleed. Play
  applies its own mask and shadow; don't add rounded corners or a drop shadow yourself.

## Feature graphic (required)

- **1024 × 500 px**, JPG or 24-bit PNG (no alpha), max 15 MB.
- Shown behind the listing header and in promotions, sometimes cropped and sometimes
  with a play button over the centre (if a promo video is set). Keep the wordmark and
  any text inside the **centre 924 × 400** safe area, and nothing important in the
  middle 200 × 200 where the play button lands.
- Content: a real frame from the visualizer, full bleed (the silk or nebula styles read
  best at small sizes), the `MuzViz Studio Pro` wordmark left or right of centre, and at
  most one short line: *Music comes to life.*
- Make it from an actual 4K export: export 16:9 at 4K, grab a frame, crop to
  1024 × 500. Darken the text side by about 30% so the wordmark has contrast.
- Don't flash-cut or strobe in the promo video either; the in-app 3 Hz flash limit
  already holds in exports.

## Phone screenshots (2 required, up to 8; 4+ recommended for featuring)

- PNG or JPG, 16:9 or 9:16, each side 320–3840 px, long side at most twice the short side.
  Capture at 1080 × 1920 (9:16) or 1080 × 2400 cropped to 9:16.
- Take them from a release-like build (no debug panel, no "-debug" version), with a
  real library and real album art you have rights to show (your own music or
  royalty-free tracks; no third-party album covers).
- Optional caption band at the top (20–25% of the height), dark, one short line each.

| # | Screen | How to get it | Caption |
|---|---|---|---|
| 1 | Fullscreen visualizer, silk style, mid-drop | Player > expand; pause on a busy frame | Music comes to life |
| 2 | Style picker with a few styles visible | Visuals > Styles | Dozens of reactive styles |
| 3 | Export settings at 4K · 60 fps, 9:16 preset | Export > Standard | Music videos up to 4K 60 fps |
| 4 | Studio timeline with clips, keyframes, a transition | Studio tab (Pro) | A full music video studio |
| 5 | Home screen with the live wallpaper running | Settings > Behavior > Live wallpaper | Your visuals on your home screen |
| 6 | Library: albums or folders with art | Library | A real music player |
| 7 | Paywall comparison table | Settings > Go Pro | One payment. No subscription. |
| 8 | Fluid or water style, landscape | Player in landscape | Touch the music |

## Tablet screenshots (recommended)

Needed for the app to be promoted to tablet and foldable users. 7" and 10" sets, same
rules; take 2–4 in landscape: the expanded layout with the navigation rail and the
two-pane library + player, and the Studio on a wide screen.

## Promo video (optional)

A YouTube URL, public or unlisted, no ads (monetisation off), 30–120 s. A 9:16
visualizer export with a cut to the Studio works; keep the audio to music you own.
