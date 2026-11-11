# Category and tags

Play Console › Grow users › Store presence › Store settings. Source: "Choose a category and tags
for your app or game"
([answer/9859673](https://support.google.com/googleplay/android-developer/answer/9859673), read
2026-11-11): one category, up to five tags, picked from Play's own list.

## Category

**Recommended: Music & Audio** (application, not game). Play describes it with music players
and audio tools, and the app is a music player first: a library, a queue, a `MediaLibraryService`
that Android Auto browses (`playback/PlaybackService`).

| Alternative | Play's description | Why not first |
|---|---|---|
| Video Players & Editors | Video players, editors, media storage | The Studio renders video but does not play or edit arbitrary video (video editing was dropped, ADR 0007) |
| Personalization | Wallpapers, live wallpapers, launchers, themes | The live wallpaper is one Pro feature, not the app |
| Entertainment | Streaming video, movies, TV, interactive entertainment | Too broad; search for a music visualizer starts in Music & Audio |

## Tags

Tags come from a list Play shows in the Console, and that list changes; it could not be read
from here. Pick up to five of the ones that exist, in this order of fit:

1. Music player
2. Music visualizer, or the nearest ("Visualizer", "Audio visualizer")
3. Video maker, or "Music video maker"
4. Live wallpaper
5. Equalizer, only if the list has nothing closer for 2 or 3

Each is true of the code: the player and library (`playback/`, `ui/LibraryScreen`), the
visualizer (`core/viz`), the MP4 render (`export/`), the wallpaper (`wallpaper/`, Pro), the
equalizer (`ui/EqualizerSettings.kt`). Do not pick a tag for something the app is not (a
streaming service, a downloader, a radio, an editor, a game).

## Contact details and the rest of Store settings

Email is required; website and phone are optional. These are the owner's own details and are
not in the repo on purpose.
