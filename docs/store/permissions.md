# Permissions: what each is for, and the foreground service declaration

Line numbers are `app/src/main/AndroidManifest.xml` and `app/src/main/java/dev/geode/` on
`origin/main` at 8fd87553d (rechecked 2026-11-12: 14 `<uses-permission>` lines; `FOREGROUND_SERVICE_MICROPHONE` joined with #1037; Show on a computer, #1048, added no permission). The released permission list is pinned in
`tools/release-permissions.txt` and checked by `tools/check-manifest.sh` in CI.

## Permissions the store page will list

| Permission | Manifest | Why | Asked | Evidence |
|---|---|---|---|---|
| `READ_MEDIA_AUDIO` | :8 | Find and play the music on the device | The first time Library opens, after a rationale | `ui/LibraryScreen.kt:210`, `data/DeviceTrackQuery.kt:36` |
| `RECORD_AUDIO` | :14 | Optional: visuals follow the microphone (Live input), another app's audio (Visualize other apps) or, for the wallpaper and the screen saver, the output mix (Hear other apps). Samples are analysed in memory and never sent; they reach a file only when the user records a video with Record while one of these is on (the MP4 on the device holds what the visual heard, `ui/RecordController.kt:38-41`) | Only when the user turns one of those on | `ui/AudioSettings.kt:70`, `ui/ExternalAudioSettings.kt:62`, `ui/PlayerScreen.kt:806,866`, `ui/LibraryEmpty.kt:132,162`, `ui/wallpaper/WallpaperOptionsActivity.kt:195`; `audio/MicCapture.kt:50`; paused in the background `ui/CaptureController.kt` `onBackground`, unless a cast holds the microphone type |
| `POST_NOTIFICATIONS` | :54 | Playback controls; the ongoing notification while capturing, rendering or casting | In context: with the first render (`ui/ExportHost.kt:157`) and with capture | same files as `RECORD_AUDIO` |
| `FOREGROUND_SERVICE` and its five types (`FOREGROUND_SERVICE_MICROPHONE` for the cast with Live input) | :27, :35, :41, :45, :52, :53 | Keep playback, casting, capture and a render running off screen, each with a visible notification | Not a runtime permission | see the declaration below |
| `INTERNET`, `ACCESS_NETWORK_STATE` | :61, :62 | Cast to TV and Show on a computer only: find the TV, talk to Google Cast, stream on the local Wi-Fi to the TV or a browser | Not runtime | the one server socket is native, `core/cast/LiveServer.cpp` (`start`), bound to the Wi-Fi address `cast/CastStream.kt:296-321`; Show on a computer reuses it (`cast/ComputerLink.kt`); CI fails on network code outside `cast/` |
| `MODIFY_AUDIO_SETTINGS` | :69 | Bit-perfect USB output to an external DAC | Not runtime | `playback/BitPerfectOutput.kt` |
| `VIBRATE` | :65 | Haptic feedback | Not runtime | `ui/theme` haptics |
| `WAKE_LOCK` | :74 | Music keeps playing with the screen off | Not runtime | Media3 playback |
| `com.android.vending.BILLING` | merged from the Billing Library | Sell and restore Pro through the Play Store app | Not runtime | `billing/PlayBillingRepository.kt` |

Not requested: camera, location, contacts, phone, SMS, storage beyond audio, exact alarms,
all-files access, query-all-packages, advertising ID, body sensors or activity recognition. The
motion sensors the app reads (accelerometer for the shake screenshot, `ui/EasterEggs.kt`;
gyroscope, `render/GyroLook.kt`; proximity, `wallpaper/DreamCover.kt`) need no permission.
Screenshots and recordings are written through MediaStore rows the app inserts itself
(`data/StillFiles.kt`, `data/RecordFiles.kt`), which needs no storage permission on Android 13+. None of the permissions above is on
Play's restricted list that needs a Permissions Declaration Form.

Two special accesses the user grants in system settings, both optional:

- **Notification access** (`audio.GeodeNotificationListener`, manifest :372): names the
  app and track playing elsewhere. Only media sessions are read.
- **Screen and audio capture consent** (MediaProjection): Android's own dialog, each time
  Visualize other apps is started.

## Microphone: what to say if Play asks

`RECORD_AUDIO` is opt-in, tied to a feature the user can see, explained before the system
dialog (`ui/PermissionRequest.kt`), and the audio never leaves the device. It stays in memory
unless the user also starts Record (Pro), whose video holds the sound the visual heard, the
microphone included when Live input is on; that file is the user's own, on their device. The
privacy policy says the same (› Record and screenshots). Play's Data safety form does not list it (see [data-safety.md](data-safety.md)).

## Foreground service declaration

Play Console › App content › Foreground service permissions. For each type Play asks for a
description, what happens if the task is interrupted, a use case from its list and a video
link ([answer/13392821](https://support.google.com/googleplay/android-developer/answer/13392821),
read 2026-11-11). The video is the owner's: an unlisted YouTube clip per type, or one clip with
the five parts (six with the microphone), each showing the user starting the task and its notification.

| Type | Service | Use case to pick | Description to paste | If interrupted | Video shows |
|---|---|---|---|---|---|
| `mediaPlayback` | `playback.PlaybackService` (:282-285), `cast.CastService` (:350-353) | Media playback | Plays the user's music after they press play and keeps it playing when the app is in the background or the screen is off, with the media notification and its controls. While the user casts to a TV or shows the visual on a computer, the same type keeps that stream running. | The music or the cast stops as soon as the user leaves the app. | Press play, go to the home screen, pull down the shade: the playback notification. Then start Cast and do the same. |
| `microphone` | `cast.CastService` (manifest :350-353), only while casting with Live input on (`cast/CastService.kt` `startInForeground`) | **Background Audio Access** (the page's one listed microphone use case; say in the description that it is live input to the visual, not voice recording) | While the user casts the visuals to a TV with Live input (microphone) turned on, keeps the microphone driving the TV's visuals when the phone's screen is off or the app is in the background. Taken only while the app is on screen, dropped when Live input or the cast stops. The audio is analysed in memory and is not recorded or sent by this service. The cast notification shows throughout. | The TV's visuals stop reacting to the room until the user reopens the app. | Turn on Live input, start Cast, go to the home screen: the cast notification and the TV still moving with the room's sound. |
| `mediaProjection` | `audio.PlaybackCaptureService` (:327-330) | Media projection | After the user turns on "Visualize other apps" and accepts Android's capture dialog, reads the audio another app is playing so the visuals can move with it. The audio is analysed in memory and never sent; it is saved only if the user records a video of the visual with Record. An ongoing notification shows for as long as it runs. | Android stops the capture and the visuals stop following the other app's music. | Turn on Visualize other apps, accept the system dialog, play music in another app, show the notification. |
| `mediaProcessing` | `export.ExportService` (:339-342) on Android 15 and newer | Media transcoding | After the user taps Render, draws the visual frame by frame and encodes it with the song into an MP4 file on the device. A notification shows progress and lets the user stop. | The render is killed partway and the video file is lost. | Studio › Render, tap Render, leave the app, show the progress notification, come back to the finished file. |
| `dataSync` | `export.ExportService` on Android 13 and 14, where `mediaProcessing` does not exist | **Local processing: Other** (a direct, user-started local task; "Import or export" is for moving files, which this is not) | The same render as above on Android 13 and 14, which have no media-processing type. Nothing is uploaded or downloaded; the task writes one local MP4 file. | The render is killed partway and the video file is lost. | The same clip as mediaProcessing, recorded on Android 13 or 14 if Play asks. |

**`mediaProjection` is the weakest fit.** Play's one listed use case for it is "Media or Content
Projection and streaming or recording with MediaProjection API", and its video rule speaks of
streaming or recording the screen to an external display. The app uses MediaProjection only for
Android's audio playback capture (the API Android requires to hear another app's sound), with
no screen capture. Pick that use case, say plainly in the description that it captures audio
only, for an on-screen visual, and show the system consent dialog in the video. If a reviewer
rejects it, the fallback is to drop "Visualize other apps" from the store build, not to
reword it.

`dataSync` is the type Play looks at hardest. The honest answer is that it is a fallback for
two Android versions and does no network transfer; say so in the description, as above.
