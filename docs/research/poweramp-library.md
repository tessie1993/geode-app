# Library and player features users value in the leading Android local player

Research for Geode's Library and Player before the Play release. The benchmark is the long-running
paid local player for Android (package `com.maxmpz.audioplayer`). We take functional ideas from it,
never its name, branding, wording or UI. Geode is offline (`INTERNET` exists only for Cast), so
anything that needs the network is marked **out of scope**.

Method, November 2026: the vendor's feature page, its forum (the feature-vote board, the knowledge
base, support threads), release notes, reviews, comparisons, and Reddit. Reddit itself blocked
direct fetches, so Reddit sentiment comes from an aggregator of Reddit reviews and from mirrored
threads. The vote counts come from scraped snapshots of the vote board, so treat them as rough.
Every Geode `file:line` below was checked with `grep -n` against commit `a2f778e`, the base of
the PR that adds this file; that PR moves some lines in the library files it changes.

## 1. Summary

- Users value the **audio fundamentals** most: gapless joins, ReplayGain and the sound itself.
  "Mandatory for me" is how one user puts gapless. Next comes **folder-first browsing** with
  control over which folders are scanned, and a **queue that interrupts the current list and then
  returns to it**.
- The most-voted wishes are about **organising**: smart or dynamic playlists (~61 votes), user-set
  row text (~69), a Favourites category (~40) and a queue-only mode (~39).
- Geode already covers most of the top ten: ExoPlayer's gapless joins, a Folders tab and a
  scanned-folder editor, a queue with play-next, ReplayGain track and album modes, a 10-band EQ, a
  single-file tag editor, smart playlists, crossfade, bit-perfect USB output and a sleep timer.
- The gaps that matter at launch are the ones being closed in this PR: the empty library, short
  files and junk, search quality and search speed. Also missing: genre and album-artist browsing, a
  folder hierarchy, synced lyrics on the Player screen, playlist export and ratings.

## 2. Ranked list

Rank = how often and how strongly users bring it up, weighted by forum votes where they exist.

| # | Feature (neutral name) | Why users value it | Sources |
|---|---|---|---|
| 1 | Gapless playback | Live albums and DJ mixes must not click or pause between tracks. Users call it mandatory, and the forum has many threads on edge cases (SD cards, MP3 encoder delay). | [HA](https://hydrogenaudio.org/index.php/topic,94101.0.html), [forum: not perfectly gapless](https://forum.powerampapp.com/topic/23283-not-perfectly-gapless/), [forum: iTunes MP3](https://forum.powerampapp.com/topic/16727-gapless-not-working-for-itunes-encoded-mp3/), [vendor](https://powerampapp.com/) |
| 2 | Folder view beside the tag library (with hierarchy) | "Folder people" browse the way they sorted their disk. They ask for a drill-down hierarchy rather than a flat list of leaf folders. | [Head-Fi](https://www.head-fi.org/threads/the-poweramp-for-android-thread.904036/post-15313158), [forum: folder selection](https://forum.powerampapp.com/topic/16481-folder-selection/), [Reddit r/plexamp (mirror)](https://redlib.hackliberty.org/r/plexamp/comments/1kxwhll/any_tweaks_for_a_folders_hierarchy_ala_poweramp), [comparison](https://bestappsforandroid.com/poweramp-vs-musicolet/) |
| 3 | Choose scanned folders, keep junk out (ringtones, notification sounds, short clips) | The most common support question is "why are my ringtones in my library" or "where are my songs". The fixes are unticking folders and an "ignore short tracks" switch. | [forum: exclude ringtones](https://forum.powerampapp.com/topic/9045-exclude-ringtone-notifications-videos-from-loading-or-playing/), [forum: remove ringtones](https://forum.powerampapp.com/topic/5759-removing-ringtones-and-notifications-from-song-list/), [KB: songs missing](https://forum.powerampapp.com/kb/en_us/faq/my-songs-are-missing-from-folderslibrary-r5/) |
| 4 | Queue that interrupts, then returns to the list | Add or play next without losing your place in the album or playlist. Users can set what happens when the queue ends. | [KB: category vs queue](https://forum.powerampapp.com/kb/en_us/guides/what-is-the-difference-between-playback-by-%E2%80%98category%E2%80%99-and-using-the-%E2%80%98queue%E2%80%99-r3), [forum: play next](https://forum.powerampapp.com/topic/9822-play-next-feature/), [forum: temporary list](https://forum.powerampapp.com/topic/20162-how-can-i-play-selected-songs-as-a-playlist-without-creating-a-permanent-playlist/), [votes: queue-only mode](https://forum.powerampapp.com/ideas/category/6-cast-your-votes-for-new-poweramp-features/page/2/) |
| 5 | ReplayGain (track and album) | Even loudness across a shuffled library without flattening albums. Audiophile forums name it together with gapless. | [HA](https://hydrogenaudio.org/index.php/topic,94101.0.html), [vendor](https://powerampapp.com/) |
| 6 | EQ and sound quality (ranked here only) | The EQ is the reason most Reddit reviewers give for choosing it ("best sound imo"). | [Reddit reviews aggregate](https://gummysearch.com/tools/best-products/music-player/), [review](https://brighthub.com/mobile/google-android/reviews/120845/), [vendor](https://powerampapp.com/) |
| 7 | Shuffle that behaves, and resume where you were | Complaints when shuffle restarts after the process is killed, or reshuffles a list unasked. Users expect the position and order to survive. | [forum: shuffle restarts](https://forum.powerampapp.com/topic/28013-shuffle-restarts-from-first-song-whenever-the-app-is-killed-and-its-extremely-annoying), [forum: reshuffle](https://forum.powerampapp.com/topic/18457-how-to-reshuffle/) |
| 8 | Smart and automatic playlists (most played, recently added, top or low rated, long tracks) | The top-voted organising wish (~61 votes). Built-in automatic lists are a selling point on the vendor's page. | [votes](https://forum.powerampapp.com/ideas/category/6-cast-your-votes-for-new-poweramp-features/page/2/), [vendor](https://powerampapp.com/) |
| 9 | Favourites as a library category | ~40 votes, and a separate "favourite folders" request. | [votes](https://forum.powerampapp.com/ideas/category/6-cast-your-votes-for-new-poweramp-features/page/2/), [forum: favourite folders](https://forum.powerampapp.com/topic/25793-add-favorite-folders-item-to-the-library-menu/) |
| 10 | Fast, forgiving search | Users find a track by typing a fragment, and Android Auto voice search depends on the same index. Bugs in the search field get reported quickly. | [forum: search field](https://forum.powerampapp.com/topic/24821-search-field-stuck/), [vendor (Auto voice search)](https://powerampapp.com/) |
| 11 | Tag editor (single and batch, with artwork) | Fixing messy tags on the phone. Comparisons count batch tag and art editing as the rival's advantage. | [comparison](https://bestappsforandroid.com/poweramp-vs-musicolet/), [slant](https://www.slant.co/versus/5433/27223/~poweramp_vs_musicolet-music-player), [Gizmodo AU](https://gizmodo.com.au/?p=454135), [vendor](https://powerampapp.com/) |
| 12 | Many sort orders and categories (genre, album artist, year, composer) | 20 categories and 22 reversible sort orders on the vendor's page. Album-artist grouping comes up with ReplayGain album mode. | [vendor](https://powerampapp.com/), [HA](https://hydrogenaudio.org/index.php/topic,94101.0.html) |
| 13 | User-set row text (which tags a list row shows) | The single most-voted idea (~69 votes). | [votes](https://forum.powerampapp.com/ideas/category/6-cast-your-votes-for-new-poweramp-features/page/2/) |
| 14 | Synced lyrics (LRC files and embedded tags) | Added in 2025 as its own screen. A half-second sync regression drew complaints at once. | [release notes](https://powerampapp.com/?p=1071), [forum: off-sync](https://forum.powerampapp.com/topic/27353-lyrics-are-off-sync-in-builds-971-976/) |
| 15 | Bit-perfect and USB DAC output | Head-Fi audience. The vendor lists four bit-perfect modes and a USB exclusive driver. | [vendor](https://powerampapp.com/), [Head-Fi](https://www.head-fi.org/threads/the-poweramp-for-android-thread.904036/post-15313158) |
| 16 | Crossfade | Expected for mixes and parties, and gets bug reports when it fails. | [forum: crossfade](https://forum.powerampapp.com/topic/9000-crossfade-not-working-at-320kbs/), [vendor](https://powerampapp.com/) |
| 17 | Android Auto (browse, search, voice, queue) | Car use. Listed as a headline feature. | [vendor](https://powerampapp.com/) |
| 18 | Widgets, lock screen and notification controls | Four resizable widgets and an optional full-screen lock screen are on the vendor's page. | [vendor](https://powerampapp.com/) |
| 19 | Sleep timer (fade, finish the current track) and A-B repeat | ~22–24 votes each for improvements, so they are in use. | [votes](https://forum.powerampapp.com/ideas/category/6-cast-your-votes-for-new-poweramp-features/page/2/) |
| 20 | Playlist import and export (M3U and friends) | Moving playlists between devices and PC players. | [forum: WMP playlists](https://forum.powerampapp.com/topic/3080-wmp-playlists/) |
| 21 | Ratings (top or low rated lists) | Feeds the automatic lists. | [vendor](https://powerampapp.com/) |
| — | Online cover and artist art download, lyrics plugins, scrobbling, radio streams | Valued, but they need the network. **Out of scope** for Geode. | [vendor](https://powerampapp.com/) |

Reddit-wide sentiment: about 4.7/5 across ~38 Reddit reviews, praised for "customization and audio
quality", with no direct complaints recorded
([aggregate](https://gummysearch.com/tools/best-products/music-player/),
[r/androidapps 2018](https://www.reddit.com/r/androidapps/comments/919ho5/community_app_suggestions_music_player/)).
Users who switch away usually go for a free app with multiple queues and batch tagging, or for a
streaming-server client ([comparison](https://bestappsforandroid.com/poweramp-vs-musicolet/)).

## 3. What Geode already has

Paths are under `app/src/main/java/dev/geode/` unless they start with `core/`.

| # | Feature | Geode evidence | State |
|---|---|---|---|
| 1 | Gapless | No code needed: ExoPlayer plays the queue as one playlist and joins it gaplessly. Crossfade is 0, "the normal gapless join", by default (`data/PlayerPrefs.kt:23-26`). The crossfade itself never overlaps (`playback/TrackCrossfade.kt:25`). | Has; check MP3 encoder-delay files on a device (`docs/DEVICE_CHECKS.md`) |
| 2 | Folder view | Folders tab (`ui/LibraryScreen.kt:248`, `FoldersTab` at `:868`). Labels are shortened leaf names, made unique (`ui/FolderTree.kt:4`). | Flat list only, no drill-down hierarchy |
| 3 | Scanned folders | Add a folder tree through SAF and rescan (`ui/MusicLibraryController.kt:297`, `:321`), editor in `ui/FolderSettings.kt:97`, card in `ui/LibraryScreen.kt:910`. The MediaStore query keeps `IS_MUSIC` only (`data/DeviceTrackQuery.kt:38`). | Partial: no per-folder exclude, no short-file filter, no actions when the library is empty |
| 4 | Queue | Play next and add to queue (`ui/QueueController.kt:150`, `:156`), remove and move (`:319`, `:326`), the queue sheet (`ui/PlayerScreen.kt:246`). Queue-end choice of stop, repeat or similar tracks (`playback/QueueEnd.kt:21`, `:41`; `playback/LibraryTree.kt:80`). | Has. Queued tracks are inserted into the one queue, not a separate queue that returns to the list |
| 5 | ReplayGain | Track and album modes read from tags (`playback/ReplayGain.kt:21`, `:69-70`). | Has (reads tags; no scan for untagged files, see #342) |
| 6 | EQ and DSP | Ten peaking bands plus a bass shelf (`core/audio/dsp/Equalizer.hpp:10`), crossfeed and limiter (`core/audio/dsp/Crossfeed.hpp:11`, `Limiter.hpp:10`), UI in `ui/EqualizerSettings.kt:23`, bass boost in `audio/AudioFxController.kt:134`. | Has (no parametric EQ, no per-output presets) |
| 7 | Resume, shuffle | Queue, index and position saved (`data/SessionStore.kt:19-22`). Bookmarks for tracks over 20 min (`playback/TrackBookmarks.kt:8`, `data/PlayerPrefs.kt:152`). Shuffle of recent plays (`ui/PlayerSession.kt:766`). | Has; check that shuffle order survives process death on a device |
| 8 | Smart playlists | Rules on title, artist, album, folder, duration, days since added, play count, favourite, with a limit (`data/SmartPlaylistStore.kt:9-16`, `:48`, matcher `:52`). Editor in `ui/SmartPlaylistEditor.kt:48`, shown in Playlists (`ui/LibraryScreen.kt:611`). | Has. No built-in "Recently added" or "Most played" lists ready-made |
| 9 | Favourites | `data/FavouritesStore.kt:5`, `FAVOURITE` rule (`data/SmartPlaylistStore.kt:9`), heart on the Player screen and in the queue. | Partial: no Favourites tab or category |
| 10 | Search | One field over the track-shaped tabs (`ui/LibraryScreen.kt:233`). Fuzzy match with a typo budget (`ui/LibraryBrowse.kt:49`, `:68`, `:82`). Global search via `LibraryScreen(onOpenSearch)` (`:146`). Voice "play X" (`ui/MainActivity.kt:170`). | Partial: no accent folding, no field ranking, filtering runs on Main (in this PR) |
| 11 | Tag editor | Single-track editor (`ui/TrackInfoEditor.kt:59`, opened at `ui/LibraryScreen.kt:436`) writing through TagLib (`data/NativeTags.kt:56`, `ui/MusicLibraryController.kt:242`). Embedded art is read (`playback/MediaArtwork.kt:27`). | Partial: no batch editing, no artwork editing (#342) |
| 12 | Sorts and categories | Sort by title, artist, album, duration and date added (`ui/LibraryBrowse.kt:11`). Tabs: Tracks, Albums, Artists, Folders, Playlists, Duplicates (`ui/LibraryScreen.kt:110-111`, `:245-250`). | Partial: no genre, album-artist or year browsing, no reverse order |
| 13 | Row text | Fixed title, artist and duration rows. | Missing (low priority) |
| 14 | Lyrics | `.lrc` sidecar and embedded tags (`ui/Lyrics.kt:32`, `:81`, `:108`), attach per track (`ui/PlayerSession.kt:491`), shown in Settings › Lyrics (`ui/LyricsSettings.kt:37`). | Partial: no synced view on the Player screen |
| 15 | Bit-perfect | USB DAC through `setPreferredMixerAttributes` on API 34+ (`playback/BitPerfectOutput.kt:41`, `:102`). | Has |
| 16 | Crossfade | `playback/TrackCrossfade.kt:25` (fade out, then fade in; never an overlap). | Has |
| 17 | Android Auto | `MediaLibraryService` browse tree (`playback/PlaybackService.kt:172`, `playback/LibraryTree.kt:19`). | Partial: browse only; no `onSearch` for in-car search |
| 18 | Widget, notification | Glance widget (`widget/NowPlayingWidget.kt:79`). Shuffle, repeat and favourite buttons in the notification (`playback/NotificationButtons.kt:31`). Media3 lock-screen controls. Headphone autoplay (`playback/HeadphoneAutoplay.kt:23`). | Has |
| 19 | Sleep timer, A-B | Fade, or finish the track (`playback/SleepTimer.kt:16`, `:34`, `:49`). A-B loop (`ui/QueueController.kt:24`). | Has |
| 20 | Playlists import/export | Manual playlists with reorder (`data/MusicPlaylistStore.kt:72`, `ui/LibraryScreen.kt:572`). Import M3U, PLS and XSPF (`data/PlaylistFormats.kt:3`, `:50`). | Partial: no export |
| 21 | Ratings | None. | Missing |
| — | Duplicates (extra; Geode has it, the benchmark does not stress it) | `ui/LibraryDuplicates.kt:24`, `ui/DuplicatesTab.kt:37`. History and play counts in `data/HistoryStore.kt:69`, `:112`. | Has; computed on Main (in this PR) |

## 4. What's missing

1. **The empty library leads nowhere.** A new user with no indexed music has nothing to do on
   Library. (In this PR, #340.)
2. **Junk in the list.** Only `IS_MUSIC` filters it. Short clips and
   app sounds that MediaStore marks as music still show up, and there is no minimum length. (In this PR, #341.)
3. **Search quality and jank.** No accent or case folding ("Beyonce" does not find "Beyoncé"), no
   ranking by field, and filter, sort, grouping and duplicates all run on Main for every keystroke.
   (In this PR, #341.)
4. **Genre and album-artist browsing**, and reverse sort orders. Compilations scatter across
   artists without album artist.
5. **Folder hierarchy.** A drill-down tree in place of the flat list of leaf folders.
6. **Synced lyrics on the Player screen.** The data loads already (`ui/PlayerSession.kt:477-491`),
   but only Settings shows it.
7. **Batch tag editing, artwork editing, and a ReplayGain scan** for untagged files. (After launch,
   #342.)
8. **Playlist export** to M3U, and a **Favourites** category or tab.
9. **Built-in automatic lists**: Recently added, Most played, Recently played as ready-made smart
   playlists.
10. **Android Auto search** (`onSearch` / `onGetSearchResult` in the library session).
11. **Ratings, user-set row text, a separate return-to-list queue, a parametric EQ and per-output
    presets.** All of these are later or never.
12. Out of scope (online): cover and artist art download, online lyrics, scrobbling, radio streams.

## 5. Recommended launch set vs later

**Launch: in this PR**

- Empty-library actions: Rescan, choose a music folder, listen to another app or the mic (#340).
- Hide files under 30 s by default, with a Library setting to show them (#341).
- Search folded for accents and case, ranked title > artist > album (#341).
- Debounced search, with filter, sort and grouping off Main and duplicates computed off Main
  (#341).

**Launch: proposals (cheap must-haves, not built)**

- *Proposal:* seed three built-in smart playlists, **Recently added**, **Most played** and
  **Recently played**. The rule fields already exist (`ADDED_DAYS_AGO`, `PLAY_COUNT`), so this
  needs only defaults in `SmartPlaylistStore`.
- *Proposal:* a **Favourites** chip or row at the top of Playlists. The store and the rule already
  exist.
- *Proposal:* **synced lyrics line on the Player screen** when an `.lrc` or embedded lyric is
  present. The `Lyrics` state is already a `StateFlow` (`ui/PlayerViewModel.kt:76`), and
  `Lyrics.indexAt` already does the timing.
- *Proposal:* a **device check of gapless on an encoder-delay MP3 album** and of **shuffle order
  after process death**, added to `docs/DEVICE_CHECKS.md`. Both are the first things this audience
  tests.

**After launch**

- Batch tag editing with artwork, and a ReplayGain scan (#342).
- Genre and album-artist tabs, and reverse sort orders (MediaStore has `GENRE` and `ALBUM_ARTIST`
  columns; `model/LibraryTrack.kt` already carries `genre`).
- Folder hierarchy view, and per-folder exclude.
- Playlist export to M3U.
- Android Auto search.
- A queue mode that returns to the list after the queue ends.
- Ratings, and user-set row text.
- A parametric EQ with per-output presets (an audio-chain change: read `docs/AUDIO_CHAIN.md`
  first).

**Never (offline app):** online art and lyrics lookup, scrobbling, cloud libraries, radio streams.
