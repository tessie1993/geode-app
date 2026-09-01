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

add_subdirectory(core)

add_library(geode SHARED
    app/src/main/cpp/geode_jni.cpp
    app/src/main/cpp/milkdrop_jni.cpp)
target_compile_options(geode PRIVATE ${GEODE_COMPILE_OPTIONS})
target_link_options(geode PRIVATE ${GEODE_LINK_OPTIONS})
target_link_libraries(geode PRIVATE geode_core projectM log android)
