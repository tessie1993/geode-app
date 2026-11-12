#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace geode::viz::frame::link {

// "Show on a computer": the Cast stream (CastStream, CastEncoder, LiveSegments) served to a browser on the
// same Wi-Fi as well as, or instead of, a TV. cast/LiveHttpServer (Kotlin) owns the socket and the bytes;
// everything it decides is here: which path is what (route), the token check, every response header, the
// page the browser loads (LinkPage.hpp), the master playlist with the codecs read from the encoder's own init
// segment, the share's state, who may connect at all (admit) and whether the TV is really playing
// (receiverHealth). Pure, no I/O; tested by core/tests/link_share_test.cpp.
//
// Security, as the owner asked ("make sure the connection is secure"): a connection is let in or refused by
// its source address before a byte is read (admit: a TV cast admits the Cast device alone, a live link the
// phone's own subnet); then every path sits under a 128-bit
// random token (SecureRandom in the shell, new per stream), compared in constant time; a wrong token, an
// unknown name, a query on an unknown name or any other path is a bare 404; only GET, HEAD and OPTIONS; no
// response caches (no-store), sniffs (nosniff) or leaks a referrer, and the page runs under a CSP with no
// inline script or style and nothing from another origin. What stays open by design: plain HTTP on the LAN
// (no certificate can be trusted for a LAN address), so while a link is shared anyone on the same Wi-Fi who
// sees the link, or the traffic, can watch; and CORS is * on the stream's own files because the TV's receiver fetches them from
// Google's https origin.

// The names the server answers under /<token>/. LiveSegments (Kotlin) writes its playlist with the same ones.
inline constexpr std::string_view kPlaylistName = "live.m3u8";
inline constexpr std::string_view kMasterName = "index.m3u8";
inline constexpr std::string_view kInitName = "init.mp4";
inline constexpr std::string_view kSegmentPrefix = "seg";
inline constexpr std::string_view kSegmentSuffix = ".m4s";
inline constexpr std::string_view kPageName = "index.html";
inline constexpr std::string_view kScriptName = "player.js";
inline constexpr std::string_view kStyleName = "player.css";
// The browser's low-latency feed: the init segment, then each new fragment the moment the encoder cuts it, on
// one long-lived response (fragmented MP4 straight into Media Source Extensions; no playlist to poll).
inline constexpr std::string_view kStreamName = "stream.mp4";
// Browsers fed at once; past it a viewer gets 503 and its page retries (the server's pool is small).
inline constexpr int kMaxStreams = 3;

// The token: 32 lowercase hex characters (16 random bytes).
inline constexpr size_t kTokenChars = 32;

enum class Resource : int {
    None = 0,
    Page = 1,
    Script = 2,
    Style = 3,
    Master = 4,
    Playlist = 5,
    Init = 6,
    Segment = 7,
    Redirect = 8,   // /<token> without its slash: 308 to /<token>/, so the page's relative names resolve
    Stream = 9,     // stream.mp4: the browser's live feed, no Content-Length, held open while the page plays
};

struct Route {
    int status = 404;
    Resource resource = Resource::None;
    int64_t sequence = -1;   // Segment only
};

// True for a well-formed token (kTokenChars lowercase hex).
bool validToken(std::string_view token);

// candidate == token, taking the same time whatever the candidate holds (length included): a timing
// difference must not tell a guesser how many leading characters were right.
bool tokenMatches(std::string_view candidate, std::string_view token);

// Where a request goes: its method and request-target as the request line has them. 200 with a resource,
// 204 for OPTIONS on a known path (the TV receiver's CORS preflight), 308 for the bare token, 405 for
// another method on a known path, 404 for everything else (a wrong or malformed token included). The feed
// (stream.mp4) has no preflight: only the page, on the phone's own origin, reads it.
Route route(std::string_view method, std::string_view target, std::string_view token);

// The response head (status line, headers, blank line) for a status and resource with a body of length
// bytes. Every head: Content-Length (not on 204, nor on the feed's 200, whose body ends when the connection
// closes), no-store, nosniff, no-referrer, Connection: close. A 503 (the feed's viewers are full) asks for a
// retry in two seconds. The
// stream's files and their preflight add CORS for the TV's receiver; the page adds its CSP and denies
// framing; the redirect names /<token>/.
std::string responseHead(int status, Resource resource, int64_t length, std::string_view token);

// The RFC 6381 codecs string of the init segment's tracks, video first: "avc1.PPCCLL" from the avcC
// (profile, constraint flags, level), "mp4a.40.N" from the esds (object type, audio object type).
// Empty when no track can be read.
std::string codecs(const uint8_t* init, size_t n);

// The master playlist a browser starts from: one variant, live.m3u8, with BANDWIDTH, RESOLUTION and
// FRAME-RATE from the cast format and CODECS from the init segment; empty until the codecs are known.
std::string masterPlaylist(const uint8_t* init, size_t n);

// The share's state, for the shell's sheet. The caller owns kStateInts ints: phase, reason.
inline constexpr int kStateInts = 2;

enum Phase : int { kIdle = 0, kStarting = 1, kLive = 2, kEnded = 3 };

// Why a share ended (state[1] in kEnded).
enum Reason : int {
    kNone = 0,
    kStream = 1,       // the stream could not start, froze or its renderer died
    kNoLan = 2,        // no Wi-Fi or Ethernet address to serve from
    kBackground = 3,   // Android refused the foreground service
    kNetwork = 4,      // the network the link names changed or went away
    kTrialEnded = 5,   // the trial ran out (after the song, as for a TV)
};

enum Event : int {
    kStart = 1,    // arg: kEntitled | kHasLan
    kReady = 2,    // the stream has enough segments to watch
    kFailed = 3,   // arg: a Reason
    kStop = 4,     // the user stopped it (the sheet, the notification)
    kShown = 5,    // the shell showed an ended share's reason
};

inline constexpr int kEntitled = 1;
inline constexpr int kHasLan = 2;

// What the shell does after a step (bits).
inline constexpr int kStartStream = 1;
inline constexpr int kStopStream = 2;
inline constexpr int kPaywall = 4;

// One event; returns the actions. Start without Pro opens the paywall and leaves the share idle; without a
// LAN it ends at once (kNoLan) without starting the stream; while starting or live it does nothing.
int step(int* state, int event, int arg);

// ---- Who may connect ----
//
// The token alone is not enough on a TV cast: the Default Media Receiver reports the media's URL, token and
// all, to every Cast sender that joins it, and any phone or laptop on the Wi-Fi can join. So the server lets a
// connection in by its source address first, closing it unread otherwise:
//   - a TV cast (kWantTv) admits the Cast device's own IPv4 address, as the Cast SDK names it, and nothing
//     else; when the SDK names no IPv4 address it falls back to the subnet rule below, or the TV could never
//     fetch (the token still guards);
//   - a live link (kWantLink) admits any site-local IPv4 peer (10/8, 172.16/12, 192.168/16) on the subnet the
//     server is bound to, the phone's own address included: the browser's address is not known in advance,
//     and the link says "anyone on this Wi-Fi with this link can watch";
//   - with neither, or from anywhere else (another subnet, a public or zero address), refused.
// Addresses are IPv4 in host order (a.b.c.d = a << 24 | b << 16 | c << 8 | d), 0 = unknown. prefix is the bound
// address's prefix length; outside 1..32 it reads as 24.
inline constexpr int kWantTv = 1;
inline constexpr int kWantLink = 2;

enum Peer : int {
    kRefused = 0,
    kReceiver = 1,   // the TV (or, with no TV address known, a subnet peer while a TV casts): its fetches count
    kBrowser = 2,    // a subnet peer let in for the link
};

bool siteLocal(uint32_t address);
int admit(uint32_t peer, uint32_t bound, int prefix, uint32_t tv, int wants);

// ---- Is the TV really playing ----
//
// The LOAD's result is dropped by Media3 and RemoteCastPlayer never reports an error, so after the LOAD the
// shell asks this every second, from the receiver's MediaStatus (player state, idle reason) and when the TV
// last fetched from the server. Times are one monotonic clock in ms; 0 = never.
//   - kReceiverError: the receiver went idle with reason ERROR (it could not play the stream);
//   - kReceiverGone: idle for another reason (finished, cancelled from another sender, interrupted by another
//     load): our stream is no longer on the TV;
//   - kReceiverUnreachable: no fetch within kReceiverFirstFetchMs of the LOAD (the TV cannot reach the phone:
//     a guest Wi-Fi's client isolation, a firewall), or none for kReceiverQuietMs while the receiver says it
//     plays or buffers (the network between them broke);
//   - else kReceiverOk. Paused, the receiver may fetch nothing for as long as it likes. The status is read only
//     after kReceiverStatusGraceMs, since until the receiver takes the LOAD it may still describe the last media.
enum Receiver : int { kReceiverOk = 0, kReceiverError = 1, kReceiverGone = 2, kReceiverUnreachable = 3 };

// MediaStatus's values (com.google.android.gms.cast.MediaStatus).
inline constexpr int kPlayerIdle = 1;
inline constexpr int kPlayerPlaying = 2;
inline constexpr int kPlayerPaused = 3;
inline constexpr int kPlayerBuffering = 4;
inline constexpr int kPlayerLoading = 5;
inline constexpr int kIdleFinished = 1;
inline constexpr int kIdleCanceled = 2;
inline constexpr int kIdleInterrupted = 3;
inline constexpr int kIdleError = 4;

inline constexpr int64_t kReceiverStatusGraceMs = 5'000;
inline constexpr int64_t kReceiverFirstFetchMs = 20'000;
inline constexpr int64_t kReceiverQuietMs = 30'000;

int receiverHealth(int64_t nowMs, int64_t loadMs, int64_t lastFetchMs, int playerState, int idleReason);

}  // namespace geode::viz::frame::link
