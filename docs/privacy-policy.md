# BeatRealms 3D — Privacy Policy

*Last updated: 11 November 2026*

BeatRealms 3D ("BeatRealms", "the app") is a music player, music visualizer and music
video studio for Android. This policy describes what the app does with your
information. The short version: **the app itself collects nothing, sells nothing and has
no servers.** Two things involve Google: buying BeatRealms Pro, which Google Play handles, and
Google's Cast component, which sends Google anonymous usage logs (see *Cast to TV* below).

## Information the app does not collect

BeatRealms itself does **not** collect, transmit, sell or share personal information. The app has:

- no user accounts, sign-in or registration;
- no analytics, telemetry, crash-reporting or advertising SDKs;
- no advertising identifier;
- no internet use except *Cast to TV*, which talks only to Google Cast and to the TV on
  your own Wi-Fi (see below).

## Information the app uses on your device

To do its job BeatRealms reads data that never leaves your device:

| What | Why | Where it goes |
|---|---|---|
| Audio files and their tags (title, artist, album, genre, year, duration, artwork) | To list, play and analyse your music | Read from device storage; stays on the device |
| Audio waveform data | Levels, spectrum, drum hits, note onsets and melody that drive the visuals | Analysed in memory, frame by frame, while it plays; nothing is stored |
| Microphone audio — only while the optional *Live input* feature is on | So the visuals can react to the room | Analysed in memory only; never recorded, stored or transmitted |
| Audio other apps are playing — only while the optional *Visualize other apps* feature is on | So the visuals can react to music from another app | Analysed in memory only; never recorded, stored or transmitted |
| Audio any app is playing — only while the live wallpaper's optional *Hear other apps* setting is on (off by default) | So the wallpaper can react to music from another app | Analysed in memory only; never recorded, stored or transmitted |
| The name of the app and track playing elsewhere — only with the optional notification access | To label what the visuals are reacting to | Shown on screen; not stored or transmitted |
| Folders you pick | Optional music folders and preset folders | Granted by you through the Android file picker; revocable at any time |
| Your presets, playlists, projects, track edits and settings | To remember your setup | The app's private storage |
| Your listening history (which tracks you played, when, how often and for how long) | To show what you played recently and how often | The app's private storage |
| Whether you own BeatRealms Pro, and when your free trial started | To unlock Pro features, including offline | The app's private storage |
| Crash details | The in-app "last crash" report you can read and copy yourself | A file in the app's private storage; nothing is sent |

## Buying BeatRealms Pro (Google Play)

BeatRealms Pro is a one-time in-app purchase made through **Google Play Billing**. When you
buy it, Google Play handles the whole payment: your payment method, billing address and
the order are processed by Google under the
[Google Payments privacy notice](https://payments.google.com/payments/apis-secure/get_legal_document?ldo=0&ldt=privacynotice)
and the [Google Privacy Policy](https://policies.google.com/privacy). BeatRealms never sees
your payment details.

To unlock Pro, the app asks the Google Play Store app on your device which purchases
your Google account owns. Play returns a purchase record (product, purchase time, order
ID and a purchase token). The app checks its signature on the device and confirms
("acknowledges") the purchase back to Google Play, as Google Play requires. It keeps that
signed purchase record on the device, with the time it last checked, so Pro keeps working
when the Play Store can't be reached; it is removed when Play reports the purchase is no
longer owned. Like the rest of the app's settings it is included in Android's own device
backup. The purchase record isn't sent anywhere else. This exchange goes between BeatRealms and the Play Store app on your
device; BeatRealms makes no internet connection of its own for it.

The free one-hour trial of Pro runs entirely on your device: the app stores when it
started, and nothing else.

## Cast to TV (Google Cast, Pro)

If you cast to a TV, your phone draws the visuals and streams them, with the sound, to the
TV over your own Wi-Fi. The stream goes from your phone straight to the TV on your local
network; it is never sent to BeatRealms or stored anywhere. Google's own Cast receiver plays
it on the TV. Only the music BeatRealms plays goes to the TV: while *Live input* or *Visualize
other apps* drives the visuals, the TV gets the picture but not that sound.

To find and connect to the TV the app uses the **Google Cast SDK**. Google says the SDK
logs how it is used (discovery and session events, device and app information) and
sends that to Google, aggregated, without identifiers that trace back to you, encrypted in
transit and kept for a short time, to improve Cast
([Google's Cast SDK disclosure](https://developers.google.com/cast/docs/android_sender/data_disclosure)).
Neither you nor BeatRealms can turn that logging off or delete it. The SDK starts when you open
the app, so that the Cast button and Android's own output switcher can find your TV, and it
looks for TVs on your network while the Cast button is on screen.

The stream is plain HTTP on your local network, with a random secret in its address, because
Google's Cast receiver cannot play a stream from a phone over HTTPS. Anyone on the same Wi-Fi
who learns that address while you cast could watch the stream. The app listens only on your
Wi-Fi address, accepts short requests only, and stops when you stop casting.

## Permissions

- **`READ_MEDIA_AUDIO`**: to find and play the music on your device.
- **`RECORD_AUDIO`**: only for the optional *Live input* and *Visualize other apps*
  features and the live wallpaper's optional *Hear other apps* setting (Android requires it
  for its playback-capture and audio-visualizer APIs). Requested when you turn one of them
  on, never at launch.
- **`POST_NOTIFICATIONS`** (Android 13+): playback controls, and the ongoing
  notifications Android requires while *Visualize other apps* or a video render runs.
- **`FOREGROUND_SERVICE`** with the media-playback, media-projection, media-processing
  and data-sync types: lets playback, casting, capture and video rendering continue while
  the app is not on screen, always with a visible notification.
- **`com.android.vending.BILLING`**: lets the app talk to the Google Play Store app to
  sell and restore BeatRealms Pro.
- **`INTERNET`** and **`ACCESS_NETWORK_STATE`**: only for *Cast to TV*: to find the TV,
  talk to Google Cast and stream to the TV on your Wi-Fi.
- **`MODIFY_AUDIO_SETTINGS`**: for bit-perfect USB audio output to an external DAC.
- **`VIBRATE`**: haptic feedback for the app's controls.
- **`WAKE_LOCK`**: keeps the music playing while the screen is off.

BeatRealms requests no camera, contacts or location permission.

## Visualize other apps (playback capture)

If you turn this on, Android shows its own consent dialog first and an ongoing
notification for as long as it runs. The device's audio output is read only to animate
the visuals. It passes through an in-memory buffer and is never saved, recorded or sent.

## Now playing (notification access)

Optionally, you can give BeatRealms *notification access* so it can show which app and track
is playing elsewhere. It reads only the media sessions Android exposes through that
access; notification contents are not read, stored or sent. You can revoke it in system
settings at any time.

## Lyrics search

*Search the web* in Settings › Library & lyrics opens your browser or search app with the
playing track's title and artist followed by "lyrics". BeatRealms does not make that search
itself; what you search for goes to the browser and search engine you use, under their
policies, and only when you tap the button.

## Video export

Exported videos are MP4 files saved where you choose. They stay on your device unless
*you* share them; the app you share to handles them under its own policy. Exports made
without Pro are limited to one minute and carry a "BeatRealms 3D" watermark.

## Backups

If Android backup is on, your BeatRealms settings, presets, playlists and listening history
may be included in your Google account backup, handled by Google. The crash file, imported textures and
recorded takes are excluded. You can turn backup off in your device settings.

Settings › Backup can also save one zip file, only when you ask it to, to a place you
choose with the system file picker. It holds your settings, presets, palettes, templates,
editor projects, playlists, library list and listening history, and your MilkDrop
presets. BeatRealms never sends it anywhere; restoring reads it back from where you saved it.

## Children

BeatRealms is not directed at children under 13. It contains no ads, no chat and no content
from other users, and the app itself collects no data from anyone, including children.

## Deleting your data

BeatRealms keeps nothing about you off your device, so there is nothing for us to delete on
request. Everything the app stores lives in its private storage. Uninstalling the app, or
*Settings → Apps → BeatRealms 3D → Storage → Clear data*, removes it. Your Pro
purchase stays with your Google account and is restored automatically after reinstalling.
Exported videos are ordinary files and are deleted like any other file.

## Third-party components

BeatRealms bundles open-source libraries (including libprojectM, AndroidX/Jetpack and Media3),
the Google Play Billing Library and the Google Cast SDK. None of them collect or transmit
data in this app, apart from the Play purchase exchange and the Cast logging described
above. The open-source notices are in the
app under Settings.

## Changes to this policy

The policy ships inside the app: *Settings → About → Privacy policy* always shows the
version that matches the app you have. If it changes, an app update carries the new
version, it is published at the same address as before, and the date above changes.

## Contact

Questions about this policy: [CONTACT_EMAIL](mailto:CONTACT_EMAIL)
