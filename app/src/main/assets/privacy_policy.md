# MuzViz Studio Pro — Privacy Policy

*Last updated: 30 October 2026*

MuzViz Studio Pro ("MuzViz", "the app") is a music player, music visualizer and music
video studio for Android. This policy describes what the app does with your
information. The short version: **MuzViz collects nothing, sends nothing and has no
servers.** The one exception is buying MuzViz Pro, which Google Play handles.

## Information the app does not collect

MuzViz does **not** collect, transmit, sell or share personal information. The app has:

- no user accounts, sign-in or registration;
- no analytics, telemetry, crash-reporting or advertising SDKs;
- no advertising identifier;
- no `INTERNET` permission. The app cannot send your data anywhere.

## Information the app uses on your device

To do its job MuzViz reads data that never leaves your device:

| What | Why | Where it goes |
|---|---|---|
| Audio files and their tags (title, artist, album, genre, year, duration, artwork) | To list, play and analyse your music | Read from device storage; stays on the device |
| Audio waveform data | Beat, tempo, key and spectrum analysis that drives the visuals | Processed in memory; a compact analysis cache is kept in the app's private storage |
| Microphone audio — only while the optional *Live input* feature is on | So the visuals can react to the room | Analysed in memory only; never recorded, stored or transmitted |
| Audio other apps are playing — only while the optional *Visualize other apps* feature is on | So the visuals can react to music from another app | Analysed in memory only; never recorded, stored or transmitted |
| The name of the app and track playing elsewhere — only with the optional notification access | To label what the visuals are reacting to | Shown on screen; not stored or transmitted |
| Folders you pick | Optional music folders and preset folders | Granted by you through the Android file picker; revocable at any time |
| Your presets, playlists, projects, track edits and settings | To remember your setup | The app's private storage |
| Whether you own MuzViz Pro, and when your free trial started | To unlock Pro features, including offline | The app's private storage |
| Crash details | The in-app "last crash" report you can read and copy yourself | A file in the app's private storage; nothing is sent |

## Buying MuzViz Pro (Google Play)

MuzViz Pro is a one-time in-app purchase made through **Google Play Billing**. When you
buy it, Google Play handles the whole payment: your payment method, billing address and
the order are processed by Google under the
[Google Payments privacy notice](https://payments.google.com/payments/apis-secure/get_legal_document?ldo=0&ldt=privacynotice)
and the [Google Privacy Policy](https://policies.google.com/privacy). MuzViz never sees
your payment details.

To unlock Pro, the app asks the Google Play Store app on your device which purchases
your Google account owns. Play returns a purchase record (product, purchase time, order
ID and a purchase token). The app checks its signature on the device and confirms
("acknowledges") the purchase back to Google Play, as Google Play requires. It keeps only
a yes/no "Pro owned" flag and the time it last checked. The purchase record isn't stored or
sent anywhere else. This exchange goes between MuzViz and the Play Store app on your
device; MuzViz itself still makes no internet connection.

The free one-hour trial of Pro runs entirely on your device: the app stores when it
started, and nothing else.

## Permissions

- **`READ_MEDIA_AUDIO`** (Android 13+) / **`READ_EXTERNAL_STORAGE`** (Android 12 and
  below): to find and play the music on your device.
- **`RECORD_AUDIO`**: only for the optional *Live input* and *Visualize other apps*
  features (Android requires it for its playback-capture API). Requested when you turn
  one of them on, never at launch.
- **`POST_NOTIFICATIONS`** (Android 13+): playback controls, and the ongoing
  notifications Android requires while *Visualize other apps* or a video render runs.
- **`FOREGROUND_SERVICE`** with the media-playback, media-projection, media-processing
  and data-sync types: lets playback, capture and video rendering continue while the app
  is not on screen, always with a visible notification.
- **`com.android.vending.BILLING`**: lets the app talk to the Google Play Store app to
  sell and restore MuzViz Pro.
- **`MODIFY_AUDIO_SETTINGS`**: for bit-perfect USB audio output to an external DAC.
- **`VIBRATE`**: haptic feedback for the app's controls.

MuzViz requests no camera, contacts, location or internet permission.

## Visualize other apps (playback capture)

If you turn this on, Android shows its own consent dialog first and an ongoing
notification for as long as it runs. The device's audio output is read only to animate
the visuals. It passes through an in-memory buffer and is never saved, recorded or sent.

## Now playing (notification access)

Optionally, you can give MuzViz *notification access* so it can show which app and track
is playing elsewhere. It reads only the media sessions Android exposes through that
access; notification contents are not read, stored or sent. You can revoke it in system
settings at any time.

## Video export

Exported videos are MP4 files saved where you choose. They stay on your device unless
*you* share them; the app you share to handles them under its own policy. Exports made
without Pro are limited to one minute and carry a "MuzViz Studio Pro" watermark.

## Backups

If Android backup is on, your MuzViz settings, presets and playlists may be included in
your Google account backup, handled by Google. The analysis cache and crash file are
excluded. You can turn backup off in your device settings.

## Children

MuzViz is not directed at children under 13. It contains no ads, no chat and no content
from other users, and it collects no data from anyone, including children.

## Deleting your data

Everything MuzViz stores lives in its private storage. Uninstalling the app, or
*Settings → Apps → MuzViz Studio Pro → Storage → Clear data*, removes it. Your Pro
purchase stays with your Google account and is restored automatically after reinstalling.
Exported videos are ordinary files and are deleted like any other file.

## Third-party components

MuzViz bundles open-source libraries (including libprojectM, AndroidX/Jetpack and Media3)
and the Google Play Billing Library. None of them collect or transmit data in this app,
apart from the Play purchase exchange described above. The open-source notices are in the
app under Settings.

## Changes to this policy

The policy ships inside the app: *Settings → About → Privacy policy* always shows the
version that matches the app you have. If it changes, an app update carries the new
version, it is published at the same address as before, and the date above changes.

## Contact

Questions about this policy: [tessie-1993@hotmail.com](mailto:tessie-1993@hotmail.com)
