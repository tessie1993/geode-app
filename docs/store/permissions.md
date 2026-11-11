# Permissions: what each is for, and the foreground service declaration

Line numbers are `app/src/main/AndroidManifest.xml` and `app/src/main/java/dev/geode/` on
`origin/main` at b8ac569b5. The released permission list is pinned in
`tools/release-permissions.txt` and checked by `tools/check-manifest.sh` in CI.

## Permissions the store page will list

| Permission | Manifest | Why | Asked | Evidence |
|---|---|---|---|---|
| `READ_MEDIA_AUDIO` | :8 | Find and play the music on the device | The first time Library opens, after a rationale | `ui/LibraryScreen.kt:159`, `data/DeviceTrackQuery.kt:23` |
| `RECORD_AUDIO` | :14 | Optional: visuals follow the microphone (Live input), another app's audio (Visualize other apps) or, for the wallpaper, the output mix (Hear other apps). Samples are analysed in memory, never stored or sent | Only when the user turns one of those on | `ui/AudioSettings.kt:58`, `ui/ExternalAudioSettings.kt:62`, `ui/PlayerScreen.kt:768,828`, `ui/LibraryEmpty.kt:132,162`, `ui/wallpaper/WallpaperOptionsActivity.kt:160`; `audio/MicCapture.kt:78`; paused in the background `ui/CaptureController.kt` `onBackground`, unless a cast holds the microphone type |
| `POST_NOTIFICATIONS` | :50 | Playback controls; the ongoing notification while capturing, rendering or casting | In context: with the first render (`ui/ExportHost.kt:158`) and with capture | same files as `RECORD_AUDIO` |
| `FOREGROUND_SERVICE` and its five types (`FOREGROUND_SERVICE_MICROPHONE` for the cast with Live input) | :27, :35, :41, :48, :49 | Keep playback, casting, capture and a render running off screen, each with a visible notification | Not a runtime permission | see the declaration below |
| `INTERNET`, `ACCESS_NETWORK_STATE` | :57, :58 | Cast to TV only: find the TV, talk to Google Cast, stream on the local Wi-Fi | Not runtime | sockets only in `cast/LiveHttpServer.kt:41-55`, `cast/CastStream.kt:249`; CI fails on network code outside `cast/` |
| `MODIFY_AUDIO_SETTINGS` | :65 | Bit-perfect USB output to an external DAC | Not runtime | `playback/BitPerfectOutput.kt` |
| `VIBRATE` | :61 | Haptic feedback | Not runtime | `ui/theme` haptics |
| `WAKE_LOCK` | :70 | Music keeps playing with the screen off | Not runtime | Media3 playback |
| `com.android.vending.BILLING` | merged from the Billing Library | Sell and restore Pro through the Play Store app | Not runtime | `billing/PlayBillingRepository.kt` |

Not requested: camera, location, contacts, phone, SMS, storage beyond audio, exact alarms,
all-files access, query-all-packages, advertising ID. None of the permissions above is on
Play's restricted list that needs a Permissions Declaration Form.

Two special accesses the user grants in system settings, both optional:

- **Notification access** (`audio.GeodeNotificationListener`, manifest :306-314): names the
  app and track playing elsewhere. Only media sessions are read.
- **Screen and audio capture consent** (MediaProjection): Android's own dialog, each time
  Visualize other apps is started.

## Microphone: what to say if Play asks

`RECORD_AUDIO` is opt-in, tied to a feature the user can see, explained before the system
dialog (`ui/PermissionRequest.kt`), and the audio never leaves memory. The privacy policy says
the same. Play's Data safety form does not list it (see [data-safety.md](data-safety.md)).

## Foreground service declaration

Play Console › App content › Foreground service permissions. For each type Play asks for a
description, what happens if the task is interrupted, a use case from its list and a video
link ([answer/13392821](https://support.google.com/googleplay/android-developer/answer/13392821),
read 2026-11-11). The video is the owner's: an unlisted YouTube clip per type, or one clip with
the five parts, each showing the user starting the task and its notification.

| Type | Service | Use case to pick | Description to paste | If interrupted | Video shows |
|---|---|---|---|---|---|
| `mediaPlayback` | `playback.PlaybackService` (:217-226), `cast.CastService` (:284-287, started at `cast/CastService.kt:216`) | Media playback | Plays the user's music after they press play and keeps it playing when the app is in the background or the screen is off, with the media notification and its controls. While the user casts to a TV, the same type keeps the cast streaming. | The music or the cast stops as soon as the user leaves the app. | Press play, go to the home screen, pull down the shade: the playback notification. Then start Cast and do the same. |
| `microphone` | `cast.CastService`, only while casting with Live input on (`cast/CastService.kt` `startInForeground`) | The closest listed one for continuing user-started microphone input in the background (else Other: live audio input for a TV visualizer) | While the user casts the visuals to a TV with Live input (microphone) turned on, keeps the microphone driving the TV's visuals when the phone's screen is off or the app is in the background. Taken only while the app is on screen, dropped when Live input or the cast stops. The audio is analysed in memory and never recorded, saved or sent. The cast notification shows throughout. | The TV's visuals stop reacting to the room until the user reopens the app. | Turn on Live input, start Cast, go to the home screen: the cast notification and the TV still moving with the room's sound. |
| `mediaProjection` | `audio.PlaybackCaptureService` (:262-265, `audio/PlaybackCaptureService.kt:128`) | Media projection | After the user turns on "Visualize other apps" and accepts Android's capture dialog, reads the audio another app is playing so the visuals can move with it. The audio is analysed in memory and never saved or sent. An ongoing notification shows for as long as it runs. | Android stops the capture and the visuals stop following the other app's music. | Turn on Visualize other apps, accept the system dialog, play music in another app, show the notification. |
| `mediaProcessing` | `export.ExportService` (:274-277, `export/ExportService.kt:86,114-116`) on Android 15 and newer | Media transcoding | After the user taps Render, draws the visual frame by frame and encodes it with the song into an MP4 file on the device. A notification shows progress and lets the user stop. | The render is killed partway and the video file is lost. | Studio › Render, tap Render, leave the app, show the progress notification, come back to the finished file. |
| `dataSync` | `export.ExportService` on Android 13 and 14, where `mediaProcessing` does not exist | Other (or "Local processing: import or export" if listed): a video render started by the user | The same render as above on Android 13 and 14, which have no media-processing type. Nothing is uploaded or downloaded; the task writes one local MP4 file. | The render is killed partway and the video file is lost. | The same clip as mediaProcessing, recorded on Android 13 or 14 if Play asks. |

`dataSync` is the type Play looks at hardest. The honest answer is that it is a fallback for
two Android versions and does no network transfer; say so in the description, as above.
