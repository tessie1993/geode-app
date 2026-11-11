# Promo video

Play takes a **YouTube URL**, not a file ("Add preview assets",
[answer/9866151](https://support.google.com/googleplay/android-developer/answer/9866151), read
2026-11-11):

- a plain watch URL: no playlist, no channel, no time code or other parameters;
- **public or unlisted**, embeddable, not age-restricted;
- **monetisation off** and no ads on the video (ads on a store video break Play's rules);
- the first 30 seconds autoplay muted in the store, so they must work without sound;
- show the real app within the first 10 seconds, and at least 80 % of the video must be the app;
- landscape reads best in the store; portrait is accepted.

Only the owner can upload it (their YouTube account) and paste the URL in Main store listing ›
Video.

## Shot list (30 seconds, landscape, no voice-over)

| Time | Shot | On screen text |
|---|---|---|
| 0-3 s | The visual full screen, already moving, on a loud part of the song | none |
| 3-8 s | Swipe to the next style, twice | "244 styles" |
| 8-12 s | Tap: the Styles menu slides up over the visual, pick a family | none |
| 12-16 s | Customize: turn one quick knob, the picture changes | "Shape every look" |
| 16-20 s | Two fingers on the visual in liquid mode | none |
| 20-25 s | Studio › Render, tap Render, cut to the finished video in the gallery | "Render it as video" |
| 25-28 s | The live wallpaper behind the home screen | "Live wallpaper, screen saver and Cast with Pro" |
| 28-30 s | The app icon and name | "BeatRealms 3D" |

Rules for the edit: no "best", "#1", prices or "download now"; anything that is Pro is labelled
Pro on screen; no tempo or BPM figures; nothing the app cannot do. Music: a track the owner
has the right to publish on YouTube (the app's reactions are to that track, so record with it
playing). Add a line in the YouTube description: "Photosensitivity warning: flashing lights."

## What the repo can record, and what it cannot

The **Store assets** workflow records raw clips with the emulator's own `screenrecord`
(`StoreReelTest.kt`): one clip per free style, full screen, 20 seconds each by default, in
portrait and landscape, uploaded as `promo-clips/` in the artifact.

They are material for a rough cut, not the video:

- the CI emulator renders in software, so the clips run well below a phone's frame rate;
- the emulator has no audio device and `screenrecord` records no sound, so the clips are silent;
- they show a visual running, not a hand using the app.

For the published video, record on a real phone: Android's built-in screen recorder with
"device audio" on, in landscape, at the phone's native resolution, with Settings › Performance
on High. The shot list above is written for that.

The foreground-service declaration also asks for video (see [permissions.md](permissions.md));
those are separate, plain clips of each notification and are not the promo video.
