cmake_minimum_required(VERSION 3.22)
project(geode_mixer_tests LANGUAGES CXX)

# Build the production mixer without the Android output/decoder stack.
enable_testing()
add_executable(mixer_tests MixerTest.cpp ../Mixer.cpp)
target_compile_features(mixer_tests PRIVATE cxx_std_17)
target_include_directories(mixer_tests PRIVATE ../../.. stubs)
if(MSVC)
    # geode_api.h uses GCC-style visibility on exported Android entry points.
    target_compile_definitions(mixer_tests PRIVATE "__attribute__(x)=")
endif()
add_test(NAME mixer_regressions COMMAND mixer_tests)
