add_library(geode_core STATIC
    api/geode_api.cpp
    analysis/WindowTable.cpp
    analysis/Spectrum.cpp
    analysis/AdaptiveRange.cpp
    analysis/LogBands.cpp
    analysis/MelBank.cpp
    analysis/Mfcc.cpp
    analysis/Chromagram.cpp
    analysis/SpectralFlux.cpp
    analysis/SuperFlux.cpp
    analysis/SpectralDescriptors.cpp
    analysis/SpectralContrast.cpp
)
target_include_directories(geode_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(geode_core PRIVATE ${GEODE_COMPILE_OPTIONS})
target_link_libraries(geode_core PUBLIC kissfft::kissfft log android)
