#include "viz/frame/LinkShare.hpp"

#include <cstdio>

#include "viz/frame/FixedRateClock.hpp"
#include "viz/frame/HlsSegments.hpp"

namespace geode::viz::frame::link {

namespace {

using hls::detail::be32;
using hls::detail::forEachBox;
using hls::detail::isType;

bool isStreamFile(Resource r) {
    return r == Resource::Master || r == Resource::Playlist || r == Resource::Init || r == Resource::Segment;
}

// A segment name, seg<decimal>.m4s, to its sequence; -1 when it is not one.
int64_t segmentSequence(std::string_view name) {
    if (name.size() <= kSegmentPrefix.size() + kSegmentSuffix.size()) return -1;
    if (name.substr(0, kSegmentPrefix.size()) != kSegmentPrefix) return -1;
    if (name.substr(name.size() - kSegmentSuffix.size()) != kSegmentSuffix) return -1;
    const std::string_view digits = name.substr(kSegmentPrefix.size(), name.size() - kSegmentPrefix.size() - kSegmentSuffix.size());
    if (digits.size() > 18) return -1;
    int64_t n = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return -1;
        n = n * 10 + (c - '0');
    }
    return n;
}

Resource named(std::string_view name) {
    if (name.empty() || name == kPageName) return Resource::Page;
    if (name == kScriptName) return Resource::Script;
    if (name == kStyleName) return Resource::Style;
    if (name == kMasterName) return Resource::Master;
    if (name == kPlaylistName) return Resource::Playlist;
    if (name == kInitName) return Resource::Init;
    if (name == kStreamName) return Resource::Stream;
    if (name == kRtcName) return Resource::Rtc;
    if (segmentSequence(name) >= 0) return Resource::Segment;
    return Resource::None;
}

const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 405: return "Method Not Allowed";
        case 411: return "Length Required";
        case 413: return "Content Too Large";
        case 414: return "URI Too Long";
        case 431: return "Request Header Fields Too Large";
        case 503: return "Service Unavailable";
        default: return "Not Found";
    }
}

const char* contentType(Resource r) {
    switch (r) {
        case Resource::Page: return "text/html; charset=utf-8";
        case Resource::Script: return "text/javascript; charset=utf-8";
        case Resource::Style: return "text/css; charset=utf-8";
        case Resource::Master:
        case Resource::Playlist: return "application/vnd.apple.mpegurl";
        case Resource::Init:
        case Resource::Segment:
        case Resource::Stream: return "video/mp4";
        case Resource::Rtc: return "application/sdp";
        default: return nullptr;
    }
}

// The page's policy: its own script, style and media only; the stream as a blob: (MSE); no plugin, frame,
// form, base or other origin.
constexpr const char* kPagePolicy =
    "default-src 'none'; script-src 'self'; style-src 'self'; media-src 'self' blob:; connect-src 'self'; "
    "img-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'";

// MPEG-4 descriptor length: up to four bytes of 7 bits, high bit = more.
bool descriptor(const uint8_t*& p, const uint8_t* end, int& tag, size_t& len) {
    if (p >= end) return false;
    tag = *p++;
    len = 0;
    for (int i = 0; i < 4; ++i) {
        if (p >= end) return false;
        const uint8_t b = *p++;
        len = (len << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) break;
    }
    return len <= static_cast<size_t>(end - p);
}

// "mp4a.40.2" from an esds body (full box): ES_Descriptor > DecoderConfigDescriptor > DecoderSpecificInfo.
std::string aacCodec(const uint8_t* body, size_t n) {
    if (n < 4) return {};
    const uint8_t* p = body + 4;
    const uint8_t* end = body + n;
    int tag = 0;
    size_t len = 0;
    if (!descriptor(p, end, tag, len) || tag != 3 || len < 3) return {};
    const uint8_t* esEnd = p + len;
    const uint8_t flags = p[2];
    p += 3;
    if (flags & 0x80) p += 2;                        // dependsOn_ES_ID
    if (flags & 0x40) p += (p < esEnd ? 1 + *p : 0);   // URL
    if (flags & 0x20) p += 2;                        // OCR_ES_ID
    if (p >= esEnd || !descriptor(p, esEnd, tag, len) || tag != 4 || len < 13) return {};
    const uint8_t* dcEnd = p + len;
    const int objectType = p[0];
    p += 13;
    int audioObjectType = 0;
    if (p < dcEnd && descriptor(p, dcEnd, tag, len) && tag == 5 && len >= 1) {
        audioObjectType = p[0] >> 3;
        if (audioObjectType == 31 && len >= 2) audioObjectType = 32 + (((p[0] & 0x07) << 3) | (p[1] >> 5));
    }
    char out[32];
    if (audioObjectType > 0) {
        std::snprintf(out, sizeof out, "mp4a.%02x.%d", objectType, audioObjectType);
    } else {
        std::snprintf(out, sizeof out, "mp4a.%02x", objectType);
    }
    return out;
}

// The codec of one sample entry, or "" when it is not one this stream uses.
std::string entryCodec(const uint8_t* type, const uint8_t* body, size_t n) {
    std::string out;
    if (isType(type, "avc1") || isType(type, "avc3")) {
        constexpr size_t kVisualEntry = 78;
        if (n < kVisualEntry) return {};
        forEachBox(body + kVisualEntry, n - kVisualEntry, [&](const uint8_t* t, const uint8_t* b, size_t len) {
            if (!isType(t, "avcC") || len < 4 || !out.empty()) return;
            char s[32];
            std::snprintf(s, sizeof s, "%.4s.%02x%02x%02x", reinterpret_cast<const char*>(type), b[1], b[2], b[3]);
            out = s;
        });
    } else if (isType(type, "mp4a")) {
        constexpr size_t kAudioEntry = 28;
        if (n < kAudioEntry) return {};
        forEachBox(body + kAudioEntry, n - kAudioEntry, [&](const uint8_t* t, const uint8_t* b, size_t len) {
            if (isType(t, "esds") && out.empty()) out = aacCodec(b, len);
        });
    }
    return out;
}

// Walks one trak to its stsd's first entry.
std::string trakCodec(const uint8_t* trak, size_t n, bool& video) {
    std::string out;
    const auto into = [](const uint8_t* p, size_t len, const char* name, auto&& f) {
        forEachBox(p, len, [&](const uint8_t* t, const uint8_t* b, size_t l) {
            if (isType(t, name)) f(b, l);
        });
    };
    into(trak, n, "mdia", [&](const uint8_t* mdia, size_t mdiaLen) {
        forEachBox(mdia, mdiaLen, [&](const uint8_t* t, const uint8_t* b, size_t l) {
            if (isType(t, "hdlr") && l >= 12) video = isType(b + 8, "vide");
        });
        into(mdia, mdiaLen, "minf", [&](const uint8_t* minf, size_t minfLen) {
            into(minf, minfLen, "stbl", [&](const uint8_t* stbl, size_t stblLen) {
                into(stbl, stblLen, "stsd", [&](const uint8_t* stsd, size_t stsdLen) {
                    if (stsdLen < 8 || be32(stsd + 4) == 0) return;
                    forEachBox(stsd + 8, stsdLen - 8, [&](const uint8_t* t, const uint8_t* b, size_t l) {
                        if (out.empty()) out = entryCodec(t, b, l);
                    });
                });
            });
        });
    });
    return out;
}

}  // namespace

bool validToken(std::string_view token) {
    if (token.size() != kTokenChars) return false;
    for (char c : token)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool tokenMatches(std::string_view candidate, std::string_view token) {
    // Always kTokenChars steps, reading a missing character as 0; the length difference is folded in too.
    unsigned diff = static_cast<unsigned>(candidate.size() ^ token.size());
    for (size_t i = 0; i < kTokenChars; ++i) {
        const unsigned a = i < candidate.size() ? static_cast<unsigned char>(candidate[i]) : 0u;
        const unsigned b = i < token.size() ? static_cast<unsigned char>(token[i]) : 0u;
        diff |= a ^ b;
    }
    return diff == 0 && validToken(token);
}

Route route(std::string_view method, std::string_view target, std::string_view token) {
    Route r;
    if (!validToken(token) || target.empty() || target[0] != '/') return r;
    const size_t cut = target.find_first_of("?#");
    const std::string_view path = target.substr(0, cut);
    const std::string_view rest = path.substr(1);
    const size_t slash = rest.find('/');
    if (!tokenMatches(rest.substr(0, slash), token)) return r;
    Resource resource = Resource::Redirect;
    int64_t sequence = -1;
    if (slash != std::string_view::npos) {
        const std::string_view name = rest.substr(slash + 1);
        if (name.find('/') != std::string_view::npos) return r;
        resource = named(name);
        if (resource == Resource::None) return r;
        if (resource == Resource::Segment) sequence = segmentSequence(name);
    }
    const bool read = method == "GET" || method == "HEAD";
    if (resource == Resource::Rtc) {
        r.status = method == "POST" ? 200 : 405;
    } else if (method == "OPTIONS" && resource != Resource::Redirect && resource != Resource::Stream) {
        r.status = 204;
    } else if (read) {
        r.status = resource == Resource::Redirect ? 308 : 200;
    } else {
        r.status = 405;
    }
    r.resource = resource;
    r.sequence = sequence;
    return r;
}

std::string responseHead(int status, Resource resource, int64_t length, std::string_view token) {
    std::string h = "HTTP/1.1 ";
    h += std::to_string(status);
    h += ' ';
    h += reasonPhrase(status);
    h += "\r\n";
    const char* type = status == 200 ? contentType(resource) : nullptr;
    if (type) {
        h += "Content-Type: ";
        h += type;
        h += "\r\n";
    }
    // The feed's body runs until the connection closes (Connection: close below ends it), so it has no length.
    const bool feed = status == 200 && resource == Resource::Stream;
    if (status != 204 && !feed) {
        h += "Content-Length: ";
        h += std::to_string(status == 200 && length > 0 ? length : 0);
        h += "\r\n";
    }
    if ((status == 200 || status == 204) && isStreamFile(resource)) {
        // The TV's receiver (Shaka, in Google's Default Media Receiver) fetches from its own https origin.
        h += "Access-Control-Allow-Origin: *\r\n";
        h += "Access-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n";
        h += "Access-Control-Allow-Headers: Range, Content-Type, Accept-Encoding\r\n";
        h += "Access-Control-Expose-Headers: Content-Length\r\n";
        h += "Access-Control-Max-Age: 600\r\n";
        // Private Network Access: the receiver's public https page fetching this private address may first
        // send a preflight asking whether the private network allows it; only the preflight carries the answer.
        if (status == 204) h += "Access-Control-Allow-Private-Network: true\r\n";
        h += "Accept-Ranges: none\r\n";
    }
    if (status == 405) {
        h += resource == Resource::Rtc      ? "Allow: POST\r\n"
             : resource == Resource::Stream ? "Allow: GET, HEAD\r\n"
                                            : "Allow: GET, HEAD, OPTIONS\r\n";
    }
    if (status == 503) h += "Retry-After: 2\r\n";
    if (status == 308 && validToken(token)) {
        h += "Location: /";
        h += token;
        h += "/\r\n";
    }
    if (status == 200 && resource == Resource::Page) {
        h += "Content-Security-Policy: ";
        h += kPagePolicy;
        h += "\r\nX-Frame-Options: DENY\r\n";
    }
    // Live: the playlist changes every second, a segment is never read twice, and the page names a token.
    h += "Cache-Control: no-store\r\n";
    h += "X-Content-Type-Options: nosniff\r\n";
    h += "Referrer-Policy: no-referrer\r\n";
    h += "Connection: close\r\n\r\n";
    return h;
}

std::string codecs(const uint8_t* init, size_t n) {
    if (!init) return {};
    std::string video, audio;
    forEachBox(init, n, [&](const uint8_t* type, const uint8_t* moov, size_t moovLen) {
        if (!isType(type, "moov")) return;
        forEachBox(moov, moovLen, [&](const uint8_t* t, const uint8_t* trak, size_t trakLen) {
            if (!isType(t, "trak")) return;
            bool isVideo = false;
            const std::string c = trakCodec(trak, trakLen, isVideo);
            if (c.empty()) return;
            std::string& slot = isVideo ? video : audio;
            if (slot.empty()) slot = c;
        });
    });
    if (video.empty() || audio.empty()) return video + audio;
    return video + "," + audio;
}

std::string masterPlaylist(const uint8_t* init, size_t n) {
    const std::string c = codecs(init, n);
    if (c.empty()) return {};
    // Peak bandwidth: the two encoders' targets and a tenth for the fMP4 boxes.
    const long long bandwidth = (static_cast<long long>(CastFormat::kVideoBitrate) + CastFormat::kAudioBitrate) * 11 / 10;
    char line[256];
    std::snprintf(line, sizeof line,
                  "#EXT-X-STREAM-INF:BANDWIDTH=%lld,CODECS=\"%s\",RESOLUTION=%dx%d,FRAME-RATE=%d.000\n", bandwidth,
                  c.c_str(), CastFormat::kWidth, CastFormat::kHeight, CastFormat::kFps);
    std::string out = "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-INDEPENDENT-SEGMENTS\n";
    out += line;
    out += kPlaylistName;
    out += '\n';
    return out;
}

int64_t bodyLength(const std::string_view* headers, size_t count, int64_t max) {
    const auto isHeader = [](std::string_view line, std::string_view name) {
        if (line.size() <= name.size() || line[name.size()] != ':') return false;
        for (size_t i = 0; i < name.size(); ++i) {
            const char c = line[i] >= 'A' && line[i] <= 'Z' ? static_cast<char>(line[i] + 32) : line[i];
            if (c != name[i]) return false;
        }
        return true;
    };
    int64_t length = kNoBody;
    for (size_t k = 0; headers && k < count; ++k) {
        const std::string_view line = headers[k];
        if (isHeader(line, "transfer-encoding")) return kBadBody;
        if (!isHeader(line, "content-length")) continue;
        if (length != kNoBody) return kBadBody;
        std::string_view v = line.substr(15);
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
        if (v.empty() || v.size() > 12) return kBadBody;
        int64_t n = 0;
        for (char c : v) {
            if (c < '0' || c > '9') return kBadBody;
            n = n * 10 + (c - '0');
        }
        length = n;
    }
    if (length > max) return kBadBody;
    return length;
}

int rtcStatus(int answerResult) {
    if (answerResult >= 0) return 200;
    // GEODE_WEBRTC_REFUSED_FULL (-4) and _NOT_READY (-5): the phone is busy, not the offer at fault.
    if (answerResult == -4 || answerResult == -5) return 503;
    return 400;
}

std::string shareUrl(uint32_t address, int port, std::string_view token) {
    if (address == 0 || port < 1 || port > 65535 || !validToken(token)) return {};
    std::string url = "http://";
    url += std::to_string(address);
    url += ':';
    url += std::to_string(port);
    url += '/';
    url += token;
    url += '/';
    return url;
}

std::string friendlyLabel(std::string_view token) {
    static constexpr const char* kFirst[16] = {"Amber", "Blue",  "Coral", "Dusk",  "Ember", "Frost", "Gold",  "Indigo",
                                               "Jade",  "Lunar", "Mint",  "Neon",  "Opal",  "Ruby",  "Silver", "Violet"};
    static constexpr const char* kSecond[16] = {"Comet", "Delta", "Echo",  "Fern",  "Glow",  "Harbor", "Lake",  "Meteor",
                                                "Nova",  "Orbit", "Pulse", "Quartz", "River", "Spark", "Tide",  "Wave"};
    // FNV-1a over the token: a stable pick that tells nothing useful about the token itself.
    uint32_t h = 2166136261u;
    for (char c : token) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    std::string label = kFirst[h & 15u];
    label += ' ';
    label += kSecond[(h >> 4) & 15u];
    return label;
}

int step(int* state, int event, int arg) {
    if (!state) return 0;
    int& phase = state[0];
    int& reason = state[1];
    const bool running = phase == kStarting || phase == kLive;
    switch (event) {
        case kStart:
            if (running) return 0;
            reason = kNone;
            if ((arg & kEntitled) == 0) {
                phase = kIdle;
                return kPaywall;
            }
            if ((arg & kHasLan) == 0) {
                phase = kEnded;
                reason = kNoLan;
                return 0;
            }
            phase = kStarting;
            return kStartStream;
        case kReady:
            if (phase == kStarting) phase = kLive;
            return 0;
        case kFailed:
            if (!running) return 0;
            phase = kEnded;
            reason = arg > kNone && arg <= kTrialEnded ? arg : kStream;
            return kStopStream;
        case kStop:
            if (phase == kEnded) {
                phase = kIdle;
                reason = kNone;
                return 0;
            }
            if (!running) return 0;
            phase = kIdle;
            reason = kNone;
            return kStopStream;
        case kShown:
            if (phase == kEnded) {
                phase = kIdle;
                reason = kNone;
            }
            return 0;
        default: return 0;
    }
}

bool siteLocal(uint32_t a) {
    return (a >> 24) == 10 || (a >> 20) == ((172u << 4) | 1u) || (a >> 16) == ((192u << 8) | 168u);
}

int admit(uint32_t peer, uint32_t bound, int prefix, uint32_t tv, int wants) {
    if (peer == 0) return kRefused;
    const bool forTv = (wants & kWantTv) != 0;
    const bool forLink = (wants & kWantLink) != 0;
    if (forTv && tv != 0 && peer == tv) return kReceiver;
    if (prefix < 1 || prefix > 32) prefix = 24;
    const uint32_t mask = prefix == 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> prefix);
    const bool onSubnet = bound != 0 && siteLocal(peer) && siteLocal(bound) && (peer & mask) == (bound & mask);
    if (!onSubnet) return kRefused;
    if (forTv && tv == 0) return kReceiver;
    if (forLink) return kBrowser;
    return kRefused;
}

int receiverHealth(int64_t nowMs, int64_t loadMs, int64_t lastFetchMs, int playerState, int idleReason) {
    if (loadMs <= 0 || nowMs < loadMs) return kReceiverOk;
    const int64_t sinceLoad = nowMs - loadMs;
    if (sinceLoad >= kReceiverStatusGraceMs && playerState == kPlayerIdle) {
        if (idleReason == kIdleError) return kReceiverError;
        if (idleReason == kIdleFinished || idleReason == kIdleCanceled || idleReason == kIdleInterrupted) return kReceiverGone;
    }
    const bool fetched = lastFetchMs > 0 && lastFetchMs >= loadMs;
    if (!fetched) return sinceLoad > kReceiverFirstFetchMs ? kReceiverUnreachable : kReceiverOk;
    const bool wantsData = playerState == kPlayerPlaying || playerState == kPlayerBuffering;
    if (wantsData && nowMs - lastFetchMs > kReceiverQuietMs) return kReceiverUnreachable;
    return kReceiverOk;
}

}  // namespace geode::viz::frame::link
