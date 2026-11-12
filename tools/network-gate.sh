#!/usr/bin/env bash
# Cast is the app's only internet use (owner, 2026-11-02), now with one native networking library, admitted on
# 2026-11-12 for "Show on a computer" over WebRTC and for nothing else: libdatachannel (third_party/libdatachannel,
# with its own libjuice, libsrtp and usrsctp) and Mbed TLS (third_party/mbedtls) for its DTLS. This gate holds the
# code to that, on every pull request (arch-map.yml) and before an APK is built (android.yml):
#   1. Kotlin and Java: no socket or HTTP API outside dev.geode.cast.
#   2. Native (core/, app/src/main/cpp): no socket, address or resolver header, and no WebRTC, ICE or TLS-socket
#      header, outside core/cast/ (WebRtcLink, the one file that drives libdatachannel).
#   3. Submodules: no other networking, TLS, analytics or crash-reporting library under third_party/.
# The release build's Maven dependencies are checked by android.yml (it needs Gradle). Usage: network-gate.sh [root]
set -uo pipefail
root=${1:-.}
fail=0

kotlin='java\.net\.(Socket|ServerSocket|DatagramSocket|HttpURLConnection|URL)([^A-Za-z]|$)|\.openConnection\(|javax\.net\.ssl\.HttpsURLConnection'
hits=$(grep -rnE "$kotlin" "$root"/app/src/main "$root"/engine/*/src/main --include=*.kt --include=*.java 2>/dev/null \
  | grep -v '/dev/geode/cast/' || true)
if [ -n "$hits" ]; then
  echo "$hits"
  echo "::error::Network APIs used outside dev.geode.cast; Cast (to a TV or a computer) is the only internet use."
  fail=1
fi

native='#[[:space:]]*include[[:space:]]*[<"](sys/socket\.h|netinet/|arpa/inet\.h|netdb\.h|ifaddrs\.h|net/if\.h|curl/|rtc/|juice/|srtp2?/|usrsctp\.h|mbedtls/net|mbedtls/ssl\.h|openssl/ssl\.h)'
hits=$(grep -rnE "$native" "$root"/core "$root"/app/src/main/cpp \
  --include=*.c --include=*.cc --include=*.cpp --include=*.h --include=*.hpp --include=*.inc 2>/dev/null \
  | grep -v "^$root/core/cast/" || true)
if [ -n "$hits" ]; then
  echo "$hits"
  echo "::error::A socket or network header outside core/cast/; only core/cast/WebRtcLink may reach the network natively."
  fail=1
fi

if [ -f "$root/.gitmodules" ]; then
  banned='curl|httplib|boost|asio|grpc|openssl|boringssl|wolfssl|gnutls|libwebsockets|websocketpp|uwebsockets|civetweb|mongoose|libnice|webrtc|gstreamer|sentry|crashpad|breakpad|firebase|bugsnag|amplitude|mixpanel|segmentio'
  hits=$(grep -E '^[[:space:]]*url[[:space:]]*=' "$root/.gitmodules" | grep -iE "$banned" || true)
  if [ -n "$hits" ]; then
    echo "$hits"
    echo "::error::A networking, TLS, analytics or crash-reporting submodule; libdatachannel and Mbed TLS (for Show on a computer) are the only ones allowed."
    fail=1
  fi
fi

[ "$fail" -eq 0 ] && echo "network gate: only cast/ reaches the network"
exit "$fail"
