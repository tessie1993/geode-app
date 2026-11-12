// Show on a computer over WebRTC (core/cast/WebRtcLink): the C ABI. Android build only, with libdatachannel.
#include <cstdint>

#include "api/geode_api.h"
#include "cast/WebRtcLink.hpp"

using geode::cast::WebRtcLink;

namespace {

WebRtcLink* from(int64_t handle) { return reinterpret_cast<WebRtcLink*>(static_cast<intptr_t>(handle)); }

}  // namespace

int64_t geode_webrtc_open(const char* bound_dotted, uint32_t bound, int prefix) {
    if (!bound_dotted || bound == 0) return 0;
    return static_cast<int64_t>(reinterpret_cast<intptr_t>(new WebRtcLink(bound_dotted, bound, prefix)));
}

void geode_webrtc_close(int64_t handle) {
    WebRtcLink* link = from(handle);
    if (!link) return;
    link->close();
    delete link;
}

void geode_webrtc_video(int64_t handle, const uint8_t* data, size_t size, int64_t pts_us, int key) {
    if (WebRtcLink* link = from(handle)) link->video(data, size, pts_us, key != 0);
}

void geode_webrtc_video_config(int64_t handle, const uint8_t* data, size_t size) {
    if (WebRtcLink* link = from(handle)) link->videoConfig(data, size);
}

void geode_webrtc_audio(int64_t handle, const uint8_t* data, size_t size, int64_t pts_us) {
    if (WebRtcLink* link = from(handle)) link->audio(data, size, pts_us);
}

int geode_webrtc_peers(int64_t handle) {
    WebRtcLink* link = from(handle);
    return link ? link->peers() : 0;
}
