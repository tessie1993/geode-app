cmake_minimum_required(VERSION 3.22.1)
project(geode C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GEODE_PAGE_FLAGS "-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${GEODE_PAGE_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${GEODE_PAGE_FLAGS}")

set(GEODE_COMPILE_OPTIONS
    -std=c++20
    -fvisibility=hidden
    -Wall
    -Wextra
    # A value-returning function that falls off its end is undefined; a call to an NDK API newer than
    # minSdk crashes on the phones below it. Neither may reach an APK as a warning.
    -Werror=return-type
    -Werror=unguarded-availability
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-flto>)
set(GEODE_LINK_OPTIONS
    -Wl,-z,max-page-size=16384
    -Wl,-z,common-page-size=16384
    $<$<CONFIG:Release>:-flto>)

# projectM v4.1.7 can only composite a frame onto the default framebuffer; the API that targets a
# caller's FBO (projectm_opengl_render_frame_fbo) exists upstream only on unreleased master.
# tools/projectm-v4.1.7-render-fbo-backport.patch backports it, and MilkdropScene relies on it.
# tools/projectm-v4.1.7-shader-fallback.patch reports which of a preset's shaders projectM swapped for
# the default one (projectm_get_dropped_shaders), so the stage can say so.
# Applied here, in order, so the submodule stays pinned to the stock tag. Idempotent: the reverse
# check recognises an already-patched tree, which is what every configure after the first (and each
# per-ABI configure the Android Gradle plugin runs) sees. The patches touch separate lines, so each
# one's check holds whether or not the other is applied.
find_package(Git REQUIRED)
set(GEODE_PROJECTM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/projectm")
set(GEODE_PROJECTM_PATCHES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/projectm-v4.1.7-render-fbo-backport.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/projectm-v4.1.7-shader-fallback.patch")
foreach(GEODE_PROJECTM_PATCH IN LISTS GEODE_PROJECTM_PATCHES)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${GEODE_PROJECTM_PATCH}"
        WORKING_DIRECTORY "${GEODE_PROJECTM_DIR}"
        RESULT_VARIABLE geode_projectm_patched
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT geode_projectm_patched EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply "${GEODE_PROJECTM_PATCH}"
            WORKING_DIRECTORY "${GEODE_PROJECTM_DIR}"
            RESULT_VARIABLE geode_projectm_apply)
        if(NOT geode_projectm_apply EQUAL 0)
            # A parallel configure may have applied it in between; only fail if it is still missing.
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${GEODE_PROJECTM_PATCH}"
                WORKING_DIRECTORY "${GEODE_PROJECTM_DIR}"
                RESULT_VARIABLE geode_projectm_patched
                OUTPUT_QUIET ERROR_QUIET)
            if(NOT geode_projectm_patched EQUAL 0)
                message(FATAL_ERROR
                    "Could not apply ${GEODE_PROJECTM_PATCH} to third_party/projectm. "
                    "Run 'git submodule update --init --recursive' and make sure the submodule is at v4.1.7 "
                    "with no other local changes.")
            endif()
        endif()
    endif()
endforeach()

# projectM as libgeode needs it: a Release shared library without the playlist library.
set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(ENABLE_PLAYLIST OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/projectm)

# TagLib must build static, linked into libgeode, but projectM above forced BUILD_SHARED_LIBS ON in
# the cache; this normal variable shadows that cache entry for everything added below.
set(BUILD_SHARED_LIBS OFF)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(VISIBILITY_HIDDEN ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/taglib)

# Oboe, the playback output's stream (core/audio/output/OboeOutput): the Prefab package of the
# com.google.oboe:oboe AAR (app/build.gradle.kts, buildFeatures.prefab). It offers one target,
# oboe::oboe, a prebuilt shared liboboe.so (16 KB aligned, built against c++_shared like this
# project), which the Android Gradle plugin packages beside libgeode.so.
find_package(oboe REQUIRED CONFIG)

# Show on a computer over WebRTC (core/cast/WebRtcLink, owner 2026-11-12): libdatachannel v0.24.6 (MPL-2.0) with
# its own submodules (libjuice for ICE, libsrtp, usrsctp, plog), and Mbed TLS v3.6.7 (Apache-2.0) for DTLS, all
# static inside libgeode, so they get its 16 KB page alignment. Unmodified submodules under third_party/; the one
# setting changed is Mbed TLS's use_srtp extension, turned on by core/cast/mbedtls_user_config.h (it also leaves out
# Mbed TLS's own socket helpers, which nothing uses). No ICE server is ever configured (WebRtcLink), so nothing in
# here talks to the internet. Not WebSocket, not the examples, not the tests; libSRTP's built-in AES-CM and
# HMAC-SHA1, the one profile libdatachannel negotiates with Mbed TLS (SRTP_AES128_CM_HMAC_SHA1_80).
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/core/cast/mbedtls_user_config.h" CACHE FILEPATH "" FORCE)
add_subdirectory(third_party/mbedtls EXCLUDE_FROM_ALL)
# libdatachannel asks for MbedTLS::MbedTLS only when no such target exists; an imported one (it appears in
# libdatachannel's export set by name only) over the three static libraries above.
add_library(MbedTLS::MbedTLS INTERFACE IMPORTED)
set_property(TARGET MbedTLS::MbedTLS PROPERTY INTERFACE_LINK_LIBRARIES mbedtls mbedx509 mbedcrypto)
set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
set(USE_NICE OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(NO_MEDIA OFF CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_SERVER ON CACHE BOOL "" FORCE)
set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
# libSRTP's own warnings-as-errors (on by default in its newer CMake) would fail on a newer NDK's warnings.
set(ENABLE_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_WARNINGS OFF CACHE BOOL "" FORCE)
set(LIBSRTP_TEST_APPS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/libdatachannel EXCLUDE_FROM_ALL)

add_subdirectory(core)

add_library(geode SHARED
    app/src/main/cpp/geode_jni.cpp
    app/src/main/cpp/geode_viz_jni.cpp
    app/src/main/cpp/geode_tags_jni.cpp
    app/src/main/cpp/geode_export_dsp_jni.cpp
    app/src/main/cpp/geode_export_jni.cpp
    app/src/main/cpp/geode_sound_jni.cpp
    app/src/main/cpp/geode_playout_jni.cpp
    app/src/main/cpp/geode_ingest_jni.cpp
    app/src/main/cpp/geode_audio_out_jni.cpp
    app/src/main/cpp/geode_webrtc_jni.cpp
    app/src/main/cpp/geode_shell_port_jni.cpp
    app/src/main/cpp/geode_render_plan_jni.cpp)
target_compile_options(geode PRIVATE ${GEODE_COMPILE_OPTIONS})
target_link_options(geode PRIVATE ${GEODE_LINK_OPTIONS})
target_link_libraries(geode PRIVATE geode_core log android)
