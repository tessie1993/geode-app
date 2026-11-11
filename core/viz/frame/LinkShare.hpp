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
// segment, and the share's state. Pure, no I/O; tested by core/tests/link_share_test.cpp.
//
// Security, as the owner asked ("make sure the connection is secure"): every path sits under a 128-bit
// random token (SecureRandom in the shell, new per stream), compared in constant time; a wrong token, an
// unknown name, a query on an unknown name or any other path is a bare 404; only GET, HEAD and OPTIONS; no
// response caches (no-store), sniffs (nosniff) or leaks a referrer, and the page runs under a CSP with no
// inline script or style and nothing from another origin. What stays open by design: plain HTTP on the LAN
// (no certificate can be trusted for a LAN address), so anyone on the same Wi-Fi who sees the link, or the
// traffic, can watch; and CORS is * on the stream's own files because the TV's receiver fetches them from
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
// another method on a known path, 404 for everything else (a wrong or malformed token included).
Route route(std::string_view method, std::string_view target, std::string_view token);

// The response head (status line, headers, blank line) for a status and resource with a body of length
// bytes. Every head: Content-Length (not on 204), no-store, nosniff, no-referrer, Connection: close. The
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

}  // namespace geode::viz::frame::link
