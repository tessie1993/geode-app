#include <cstring>
#include <string>

#include "api/geode_api.h"
#include "util/QrCode.hpp"
#include "viz/frame/LinkPage.hpp"
#include "viz/frame/LinkShare.hpp"

namespace share = geode::viz::frame::link;
namespace qr = geode::util::qr;

static_assert(share::kTokenChars == GEODE_LINK_TOKEN_CHARS && share::kStateInts == GEODE_LINK_STATE);
static_assert(static_cast<int>(share::Resource::Segment) == GEODE_LINK_SEGMENT &&
              static_cast<int>(share::Resource::Redirect) == GEODE_LINK_REDIRECT &&
              static_cast<int>(share::Resource::Page) == GEODE_LINK_PAGE &&
              static_cast<int>(share::Resource::Stream) == GEODE_LINK_STREAM && share::kMaxStreams == GEODE_LINK_MAX_STREAMS);
static_assert(share::kEnded == GEODE_LINK_PHASE_ENDED && share::kTrialEnded == GEODE_LINK_REASON_TRIAL &&
              share::kNetwork == GEODE_LINK_REASON_NETWORK && share::kShown == GEODE_LINK_EVENT_SHOWN);
static_assert(share::kStartStream == GEODE_LINK_DO_START_STREAM && share::kStopStream == GEODE_LINK_DO_STOP_STREAM &&
              share::kPaywall == GEODE_LINK_DO_PAYWALL && share::kHasLan == GEODE_LINK_HAS_LAN);
static_assert(share::kWantTv == GEODE_LINK_WANT_TV && share::kWantLink == GEODE_LINK_WANT_LINK &&
              share::kRefused == GEODE_LINK_PEER_REFUSED && share::kReceiver == GEODE_LINK_PEER_RECEIVER &&
              share::kBrowser == GEODE_LINK_PEER_BROWSER);
static_assert(share::kReceiverOk == GEODE_LINK_RECEIVER_OK && share::kReceiverError == GEODE_LINK_RECEIVER_ERROR &&
              share::kReceiverGone == GEODE_LINK_RECEIVER_GONE &&
              share::kReceiverUnreachable == GEODE_LINK_RECEIVER_UNREACHABLE);
static_assert(qr::kMaxBytes == GEODE_QR_MAX_BYTES && qr::kMaxSide == GEODE_QR_MAX_SIDE);

namespace {

// The size-query convention of geode_hls_playlist: the length, written only when it fits with its NUL.
int copyOut(const std::string& text, char* out, int cap) {
    const int len = static_cast<int>(text.size());
    if (out && cap > 0) {
        if (len + 1 > cap) {
            out[0] = '\0';
        } else {
            std::memcpy(out, text.data(), text.size());
            out[len] = '\0';
        }
    }
    return len;
}

}  // namespace

int geode_link_route(const char* method, const char* target, const char* token, int* resource, int64_t* sequence) {
    const share::Route r = share::route(method ? method : "", target ? target : "", token ? token : "");
    if (resource) *resource = static_cast<int>(r.resource);
    if (sequence) *sequence = r.sequence;
    return r.status;
}

int geode_link_response_head(int status, int resource, int64_t length, const char* token, char* out, int cap) {
    const auto r = resource >= 0 && resource <= GEODE_LINK_STREAM ? static_cast<share::Resource>(resource) : share::Resource::None;
    return copyOut(share::responseHead(status, r, length, token ? token : ""), out, cap);
}

const char* geode_link_asset(int resource, size_t* size) {
    std::string_view text;
    switch (resource) {
        case GEODE_LINK_PAGE: text = share::kPageHtml; break;
        case GEODE_LINK_SCRIPT: text = share::kPageJs; break;
        case GEODE_LINK_STYLE: text = share::kPageCss; break;
        default: break;
    }
    if (size) *size = text.size();
    return text.empty() ? nullptr : text.data();
}

int geode_link_master_playlist(const uint8_t* init, size_t init_size, char* out, int cap) {
    return copyOut(share::masterPlaylist(init, init_size), out, cap);
}

int geode_link_step(int* state, int event, int arg) { return share::step(state, event, arg); }

int geode_link_admit(uint32_t peer, uint32_t bound, int prefix, uint32_t tv, int wants) {
    return share::admit(peer, bound, prefix, tv, wants);
}

int geode_link_receiver_health(int64_t now_ms, int64_t load_ms, int64_t last_fetch_ms, int player_state, int idle_reason) {
    return share::receiverHealth(now_ms, load_ms, last_fetch_ms, player_state, idle_reason);
}

int geode_qr_encode(const uint8_t* data, int size, uint8_t* out, int cap) {
    if (size < 0 || size > GEODE_QR_MAX_BYTES || (size > 0 && !data)) return 0;
    const qr::Symbol s = qr::encode(data, static_cast<size_t>(size));
    if (s.side == 0 || !out || cap < s.side * s.side) return 0;
    std::memcpy(out, s.modules.data(), s.modules.size());
    return s.side;
}
