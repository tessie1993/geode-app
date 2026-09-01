add_library(geode_core STATIC
    api/geode_api.cpp
    analysis/WindowTable.cpp    analysis/Spectrum.cpp
    analysis/AdaptiveRange.cpp
)
target_include_directories(geode_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(geode_core PRIVATE ${GEODE_COMPILE_OPTIONS})
target_link_libraries(geode_core PUBLIC kissfft::kissfft log android)
