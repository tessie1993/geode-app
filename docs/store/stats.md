# Stats: the numbers the listing may quote

Every number comes from the code or the catalogue and is recounted by `tools/store-stats.sh`
(run from the repo root). Recounted on `origin/main` at b1ac6a75a, 2026-11-11 (only the permission count moved, 13 to 14, with the Cast microphone type):

```
styles                      244
  by kind                   SHADER 181, CYMATICS 11, SILK 10, MYCELIUM 10, LIFE 10, ACID 10, FLUID 9, WATER 1, MILKDROP 1, CURL_FLOW 1
style families              10 (120 styles, 12 each)
free styles                 67 (shader 181 - family non-openers 110 - neon Pro 4)
Pro-only styles             177
bundled looks (presets)     763 (12 generic x 61 loose shader styles + 31 hand-tuned)
bundled MilkDrop presets    6
Customize categories        9
export resolutions          HD720 FHD1080 UHD4K
export codecs               H264 HEVC
export frame rates          (24, 25, 30, 50, 60)
export aspect ratios        16:9 9:16 1:1 4:5 4:3 21:9
export destinations         TIKTOK SHORTS FEED SQUARE YOUTUBE MASTER CUSTOM
free export cap             1080p, 60_000L ms
minSdk / targetSdk          33 / 36
version base / code base    1.8.1 / 33
permissions                 14
```

How to read them, and what the listing says:

| Number | Counted from | Command | In the listing as |
|---|---|---|---|
| 244 styles | `core/tests/catalogue.txt`, the native catalogue's text, pinned by `core/tests/catalogue_test.cpp` | `grep -c '^style\|' core/tests/catalogue.txt` | "244 visual styles". MilkDrop counts as one style, whatever presets are loaded |
| 10 families of 12 | the same file's `family\|` rows | `grep -c '^family\|' core/tests/catalogue.txt` | "ten style families" |
| 67 free styles | shader styles, minus each family's eleven non-openers, minus the four Pro neon styles (`pro/FreeTier.kt:20-45`) | the script's arithmetic | "more than 60 styles" free. Rounded down on purpose, so one style moving to Pro does not make the page false |
| 763 bundled looks | `core/viz/CataloguePresets.cpp`: 12 generic looks for each of the 61 loose shader styles, plus 31 hand-tuned; `core/tests/presets_test.cpp:73` asserts the same sum | the script | "more than 700 bundled looks" |
| 6 MilkDrop presets | `app/src/main/assets/milk/` | `find app/src/main/assets/milk -name '*.milk' \| wc -l` | not quoted; the listing says "bring your own .milk files" |
| 9 Customize categories | `ui/CustomizeCategory.kt` | the script | "nine groups" |
| Export: 720p, 1080p, 4K | `ExportQuality`, `export/VideoExporter.kt` | the script | as written |
| Export: H.264, HEVC | `ExportCodec`, same file. HEVC falls back to H.264 on a device with no HEVC encoder | the script | "H.264 or HEVC" |
| Export: 24, 25, 30, 50, 60 fps | `EXPORT_FPS_OPTIONS`, `data/ExportPrefs.kt` | the script | "24 to 60 frames per second" |
| Export: six shapes | `ExportRatio`, `export/VideoExporter.kt` | the script | "widescreen, vertical, square and other shapes" |
| Container | MP4 only (`MediaMuxer`, `export/`) | | "an MP4 file" |
| Free export | 1080p, 30 fps, one minute, watermark (`pro/FreeTier.kt:76-82`) | the script | as written |
| Android 13 or newer | `minSdk = 33`, `app/build.gradle.kts` | the script | "Requires Android 13 or newer" |
| Target API 36 | `targetSdk = 36` | the script | not quoted; Play's target-API floor is checked in CI (`tools/check-manifest.sh`) |

Not counted, and not claimed anywhere: tempo or BPM figures, a frame rate the visual runs at,
a number of "effects", download or rating figures.

`app/src/main/assets/gl_transitions.json` lists 122 transitions for Studio's clip transitions
(`editor/ClipTransition`). The editor that offered them was dropped (ADR 0007) and it was not
checked that a user can still reach them, so the listing does not mention them.
