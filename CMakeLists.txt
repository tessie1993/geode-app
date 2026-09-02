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
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-flto>)
set(GEODE_LINK_OPTIONS
    -Wl,-z,max-page-size=16384
    -Wl,-z,common-page-size=16384
    $<$<CONFIG:Release>:-flto>)

# projectM v4.1.7 can only composite a frame onto the default framebuffer; the API that targets a
# caller's FBO (projectm_opengl_render_frame_fbo) exists upstream only on unreleased master.
# tools/projectm-v4.1.7-render-fbo-backport.patch backports it, and MilkdropScene relies on it.
# Applied here so the submodule stays pinned to the stock tag. Idempotent: the reverse check
# recognises an already-patched tree, which is what every configure after the first (and each
# per-ABI configure the Android Gradle plugin runs) sees.
find_package(Git REQUIRED)
set(GEODE_PROJECTM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/projectm")
set(GEODE_PROJECTM_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/tools/projectm-v4.1.7-render-fbo-backport.patch")
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

# Same values .github/workflows/native-libs.yml passed when the engine was a prebuilt blob.
set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(ENABLE_PLAYLIST ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/projectm)

# kissfft refuses BUILD_SHARED_LIBS together with KISSFFT_STATIC; the normal variable shadows the forced cache entry projectM needed.
set(BUILD_SHARED_LIBS OFF)
set(KISSFFT_DATATYPE "float" CACHE STRING "" FORCE)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/kissfft)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(VISIBILITY_HIDDEN ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/taglib)

add_subdirectory(third_party/oboe)

add_subdirectory(core)

add_library(geode SHARED
    app/src/main/cpp/geode_jni.cpp
    app/src/main/cpp/geode_viz_jni.cpp
    app/src/main/cpp/geode_tags_jni.cpp
    app/src/main/cpp/geode_player_jni.cpp)
target_compile_options(geode PRIVATE ${GEODE_COMPILE_OPTIONS})
target_link_options(geode PRIVATE ${GEODE_LINK_OPTIONS})
target_link_libraries(geode PRIVATE geode_core log android)
