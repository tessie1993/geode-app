/* Mbed TLS settings for Show on a computer over WebRTC (core/cast/WebRtcLink), appended to Mbed TLS's default
 * configuration through MBEDTLS_USER_CONFIG_FILE (root CMakeLists.txt). Only two changes:
 *   - the DTLS use_srtp extension (RFC 5764), which DTLS-SRTP needs to agree the media keys; off by default;
 *   - no MBEDTLS_NET_C: Mbed TLS's own socket helpers, which nothing here calls (libjuice owns the one socket). */
#define MBEDTLS_SSL_DTLS_SRTP
#undef MBEDTLS_NET_C
