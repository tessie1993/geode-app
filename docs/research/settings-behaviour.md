# Settings: what users expect from a music player and visualizer

Research for the Settings rebuild (#227; #336, #337, #338). Goal: rank the behaviour, gesture and
preference settings people expect in a leading Android music player or visualizer, then set
Geode's groups, what sits in each group's Basics, and what drops into its sections. Only options
the app honours, or this PR implements, get a row.

## Sources looked at

- **Poweramp.** Settings is a list of groups (Look and Feel, Audio, Headset/Bluetooth, Lock
  screen, Library, Misc), each a page. Headset/Bluetooth holds pause on disconnect and resume on
  wired or Bluetooth connect; Audio holds crossfade, gapless, ReplayGain, the equalizer and output.
  ([Guiding Tech](https://www.guidingtech.com/best-poweramp-settings-tricks/),
  [setup notes](https://shapeshifter32.substack.com/p/fine-tuning-wired-headphones-for))
- **Musicolet.** A Handsfree section (headset buttons, resume on connect), crossfade (a Pro extra),
  several queues, sleep timer with recent timers, Now Playing look.
  ([release notes](https://www.apkmirror.com/apk/krosbits/musicolet-music-player-free-no-ads/))
- **Pixel and YouTube Music conventions.** Short top-level list, a one-line summary per row,
  playback first, "about" and housekeeping last; tablets get a centred column, not a stretched
  list. ([9to5Google on YouTube Music's settings](https://9to5google.com/2022/05/30/youtube-music-settings/))
- **Symfonium.** Playback › Transitions: crossfade length, fade on pause/resume, gapless.
  ([Symfonium docs](https://support.symfonium.app/t/settings-playback-transitions/7559))
- **Android's own settings guidelines.** Group related settings, keep a screen to 10–15 items, put
  rarely used ones behind a further step, and keep a single switch for a feature rather than
  duplicating it. ([AOSP settings guidelines](https://source.android.com/docs/core/settings/settings-guidelines))
- **Android accessibility.** "Remove animations" sets the animator duration scale to 0; apps are
  expected to follow it, not to offer a second switch.

## Ranked: what users expect to find

1. **Crossfade and fades between tracks** (Poweramp, Musicolet, Symfonium, Spotify). Expected
   as a slider in seconds with an Off position, off by default for gapless albums.
2. **Headphones and Bluetooth behaviour**: pause when unplugged, resume when connected.
3. **Sleep timer**, ideally finishing the current track.
4. **Resume where I left off** on launch.
5. **Equalizer and volume levelling** (ReplayGain), then output options (bit-perfect USB).
6. **Start screen / open on**: the last screen, or a chosen one.
7. **Keep the screen on** while the visual plays (visualizers, video apps).
8. **What happens when the queue ends**: stop, repeat, or keep playing similar music.
9. **Remember shuffle and repeat** across restarts.
10. **Notification and lock screen buttons** (shuffle, repeat, favourite).
11. **Gestures** on the now-playing or visual surface, each remappable (Poweramp's swipe actions).
12. **Appearance**: accent, text size, layout; and **performance or quality** for a visualizer.
13. **Library folders** and **lyrics**.
14. **Backup, about, licences** last.

Less expected, and not offered: shake actions, volume keys changing the style, "Back twice to
leave", a separate in-app reduced-motion switch, Visuals orientation (the owner did not choose it).
Geode stores some of these (`BehaviourPrefs`) but nothing reads them yet, so they get no row.

## Geode's groups

The list order follows the ranking. Each group: Basics always open, sections below that drop
open, several at once. Max width 600 dp, centred.

| Group | Basics (always open) | Sections |
|---|---|---|
| Playback | Crossfade (Off, 1–12 s), fade on play/pause, pause when unplugged, resume on launch, sleep timer | Speed & pitch · Volume levelling · Equalizer · Output and more (bit-perfect, skip silence, Player's screen on, long tracks) |
| Behaviour & gestures | Open on, keep the screen on in Visuals, play when headphones connect, when the queue ends, remember shuffle and repeat | Gestures on the visual (one dropdown per gesture) · Notification buttons · Picture-in-picture · What you're here for |
| Visuals | Note & key reactions, flashing and motion (states the flash clamp; reduced motion follows Android) | Touch · Connected display · Wallpaper & screen saver |
| Sound input | Microphone, other apps' audio | How the visual listens (sound tuning, preset morph) |
| Look | Accent, interface sounds | Text · Layout |
| Performance | Quality preset | Details |
| Library & lyrics | Music folders | Lyrics · Preset and video folders |
| Export | Points to Studio › Render | — |
| Tutorials & help | Tutorials | Help topics |
| Backup & about | Backup | About |

**No Auto group.** Visuals' quick settings already hold Auto, the style timer, its sources and the
visual playlist, where their effect is on screen.

**Reduced motion** follows Android's "Remove animations" alone (`data/SystemMotion`): the UI motion
scheme, the live renderer (live, through a content observer), the paywall, exports, casts and the
wallpaper read the one flag. Visuals › Basics shows its state and opens Android's page.
