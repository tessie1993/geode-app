// The cast's HTTP server (core/cast/LiveServer): the C ABI. Android build only (sockets).
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "api/geode_api.h"
#include "cast/LiveServer.hpp"
#include "cast/WebRtcLink.hpp"

using geode::cast::LiveServer;
using geode::cast::WebRtcLink;

namespace {

LiveServer* from(int64_t handle) { return reinterpret_cast<LiveServer*>(static_cast<intptr_t>(handle)); }

std::string text(const LiveServer& server, int which) {
    switch (which) {
        case GEODE_CAST_TEXT_PLAYLIST_URL: return server.playlistUrl();
        case GEODE_CAST_TEXT_SHARE_URL: return server.shareUrl();
        case GEODE_CAST_TEXT_LABEL: return server.label();
        case GEODE_CAST_TEXT_REFUSALS: return server.refusals();
        default: return {};
    }
}

}  // namespace

int64_t geode_cast_server_open(const char* bound_dotted, uint32_t bound, int prefix, int64_t webrtc) {
    if (!bound_dotted || bound == 0) return 0;
    try {
        WebRtcLink* link = reinterpret_cast<WebRtcLink*>(static_cast<intptr_t>(webrtc));
        auto answer = [link](std::string_view offer, std::string& out) -> int {
            try {
                return link ? link->answer(offer, out) : GEODE_WEBRTC_REFUSED_FAILED;
            } catch (...) {
                return GEODE_WEBRTC_REFUSED_FAILED;
            }
        };
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(new LiveServer(bound_dotted, bound, prefix, answer)));
    } catch (...) {
        return 0;
    }
}

int geode_cast_server_start(int64_t handle) {
    LiveServer* server = from(handle);
    if (!server) return 0;
    try {
        return server->start();
    } catch (...) {
        return 0;
    }
}

void geode_cast_server_stop(int64_t handle) {
    LiveServer* server = from(handle);
    if (!server) return;
    try {
        server->stop();
    } catch (...) {
    }
}

void geode_cast_server_close(int64_t handle) {
    LiveServer* server = from(handle);
    if (!server) return;
    try {
        server->stop();
    } catch (...) {
    }
    delete server;
}

void geode_cast_server_admit(int64_t handle, uint32_t tv, int wants) {
    if (LiveServer* server = from(handle)) server->admit(tv, wants);
}

void geode_cast_server_write(int64_t handle, const uint8_t* data, size_t size) {
    LiveServer* server = from(handle);
    if (!server || !data || size == 0) return;
    try {
        server->segments().write(data, size);
    } catch (...) {
        // Out of memory: the bytes are lost, no segment is cut and the freeze watchdog ends the cast.
    }
}

int geode_cast_server_text(int64_t handle, int which, char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
    const LiveServer* server = from(handle);
    if (!server) return 0;
    try {
        const std::string value = text(*server, which);
        if (value.size() + 1 > static_cast<size_t>(cap)) return 0;
        std::memcpy(out, value.data(), value.size());
        out[value.size()] = '\0';
        return static_cast<int>(value.size());
    } catch (...) {
        out[0] = '\0';
        return 0;
    }
}

void geode_cast_server_state(int64_t handle, int64_t* out) {
    if (!out) return;
    for (int i = 0; i < GEODE_CAST_STATE; ++i) out[i] = 0;
    const LiveServer* server = from(handle);
    if (!server) return;
    try {
        const geode::cast::LiveSegments& stream = server->segments();
        out[GEODE_CAST_STATE_LAST_FETCH_MS] = server->lastReceiverFetchMs();
        out[GEODE_CAST_STATE_SEGMENT_AGE_MS] = stream.msSinceLastSegment();
        out[GEODE_CAST_STATE_READY] = stream.playlist().empty() ? 0 : 1;
        out[GEODE_CAST_STATE_AS_MUXED] = stream.servedAsMuxed();
        out[GEODE_CAST_STATE_BROKEN] = stream.broken() ? 1 : 0;
    } catch (...) {
    }
}
