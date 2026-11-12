add_library(geode_core STATIC
    api/geode_viz_api.cpp
    api/geode_dsp_api.cpp
    api/geode_tags_api.cpp
    api/geode_library_api.cpp
    api/geode_catalogue_api.cpp
    api/geode_params_api.cpp
    api/geode_colour_api.cpp
    api/geode_randomize_all_api.cpp
    api/geode_shake_api.cpp
    api/geode_macro_bank_api.cpp
    api/geode_sound_api.cpp
    api/geode_underlay_api.cpp
    api/geode_frame_api.cpp
    api/geode_link_api.cpp
    viz/frame/LinkShare.cpp
    viz/frame/WebRtcRule.cpp
    util/QrCode.cpp
    api/geode_surface_api.cpp
    api/geode_auto_api.cpp
    api/geode_stage_step_api.cpp
    api/geode_playout_api.cpp
    api/geode_ingest_api.cpp
    api/geode_audio_out_api.cpp
    sound/Fft.cpp
    sound/Emotion.cpp
    sound/Form.cpp
    sound/Harmony.cpp
    sound/Rhythm.cpp
    sound/SoundEngine.cpp
    sound/interp/PlayoutClock.cpp
    sound/Tuning.cpp
    library/Tags.cpp
    library/Search.cpp
    audio/ingest/AiffReader.cpp
    audio/ingest/PcmRing.cpp
    audio/ingest/TrackFeed.cpp
    audio/ingest/LayerMix.cpp
    audio/ingest/SnapshotStitch.cpp
    audio/output/OutputFifo.cpp
    audio/output/OboeOutput.cpp
    audio/dsp/DspProcessor.cpp
    audio/dsp/GainStage.cpp
    audio/dsp/Equalizer.cpp
    audio/dsp/Crossfeed.cpp
    audio/dsp/Limiter.cpp
    audio/dsp/PcmResampler.cpp
    audio/dsp/Loudness.cpp
    api/geode_export_dsp_api.cpp
    signal/EngineSettings.cpp
    signal/Resolver.cpp
    api/geode_export_pass_api.cpp
    api/geode_export_api.cpp
    api/geode_record_api.cpp
    audio/dsp/PcmConvert.cpp
    audio/dsp/PcmTiming.cpp
    output/FrameClock.cpp
    output/LoopPlan.cpp
    output/LoudnessAdvice.cpp
    output/EncodePlan.cpp
    output/RecordPlan.cpp
    output/Recorder.cpp
    output/Chapters.cpp
    api/geode_output_api.cpp
    viz/ShaderSource.cpp
    viz/GlCaps.cpp
    viz/GlProber.cpp
    viz/GlProfile.cpp
    viz/Compute.cpp
    viz/Sha256.cpp
    viz/ProgramBinaryCache.cpp
    viz/Program.cpp
    viz/ProgramStager.cpp
    viz/StyleSchedule.cpp
    viz/ShaderOverrides.cpp
    viz/Texture.cpp
    viz/Framebuffer.cpp
    viz/Quad.cpp
    viz/Params.cpp
    viz/ParamsFields.cpp
    viz/ConfigFields.cpp
    viz/Lfo.cpp
    viz/ModTargets.cpp
    viz/MacroBank.cpp
    viz/MacroControls.cpp
    viz/Director.cpp
    viz/DirectorReach.cpp
    viz/LayerState.cpp
    viz/NoteResponse.cpp
    viz/UiBlurPass.cpp
    viz/DivePass.cpp
    viz/Underlay.cpp
    viz/frame/ExportPasses.cpp
    viz/frame/EdgeGlowPass.cpp
    viz/frame/RecordTap.cpp
    viz/frame/CastTee.cpp
    api/geode_cast_tee_api.cpp
    viz/Adsr.cpp
    viz/MotionField.cpp
    viz/ReactionDna.cpp
    viz/Reactions.cpp
    viz/CameraRig.cpp
    viz/VisualSafety.cpp
    viz/ThermalGovernor.cpp
    viz/TouchField.cpp
    viz/Transition.cpp
    viz/CompositeGrade.cpp
    viz/CompositePass.cpp
    viz/TrailPass.cpp
    viz/Catalogue.cpp
    viz/CataloguePresets.cpp
    viz/ParamRandomizer.cpp
    viz/CustomizeMap.cpp
    viz/RandomizeAll.cpp
    viz/WaveRandomizer.cpp
    viz/SceneRegistry.cpp
    viz/Renderer.cpp
    viz/RendererFrame.cpp
    viz/scenes/ShaderScene.cpp
    viz/SoundUniforms.cpp
    viz/BlueNoise.cpp
    viz/Overlays.cpp
    viz/compute/SimField.cpp
    viz/compute/SimGlsl.cpp
    viz/compute/SimPass.cpp
    viz/fluid/FluidBuffers.cpp
    viz/fluid/FluidMath.cpp
    viz/fluid/RippleMath.cpp
    viz/fluid/FluidChoreography.cpp
    viz/fluid/FluidEmitters.cpp
    viz/fluid/FluidSim.cpp
    viz/fluid/FluidParticles.cpp
    viz/fluid/FluidLook.cpp
    viz/fluid/RippleSim.cpp
    viz/fluid/FlowField.cpp
    viz/liquid/LiquidMath.cpp
    viz/liquid/LiquidPass.cpp
    viz/world/WorldGenome.cpp
    viz/world/MorphDriver.cpp
    viz/world/WorldAssets.cpp
    viz/scenes/StyleCatalog.cpp
    viz/scenes/CymaticsMath.cpp
    viz/scenes/CymaticsScene.cpp
    viz/scenes/SilkScene.cpp
    viz/scenes/LifeScene.cpp
    viz/scenes/MycoScene.cpp
    viz/scenes/AcidScene.cpp
    viz/scenes/FluidSceneBase.cpp
    viz/scenes/FluidScene.cpp
    viz/scenes/CurlFlowScene.cpp
    viz/scenes/WaterScene.cpp
    viz/scenes/MilkdropScene.cpp
    util/Json.cpp
)
target_include_directories(geode_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# TagLib only exports its include paths for the install tree; the generated taglib_config.h sits in its binary dir.
target_include_directories(geode_core PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/taglib/taglib
    ${CMAKE_SOURCE_DIR}/third_party/taglib/taglib/toolkit
    ${CMAKE_BINARY_DIR}/third_party/taglib)
target_compile_options(geode_core PRIVATE ${GEODE_COMPILE_OPTIONS})
# nativewindow: AHardwareBuffer, the GPU buffers Cast's tee hands the stage's picture over in (viz/frame/CastTee).
target_link_libraries(geode_core PUBLIC projectM tag GLESv3 EGL log android nativewindow)
# The playback output's stream (audio/output/OboeOutput.cpp); found by the root CMakeLists.txt.
target_link_libraries(geode_core PUBLIC oboe::oboe)

# glTF import (core/scene/object, #474): cgltf is header-only (its one implementation is compiled in
# GltfImport.cpp); meshoptimizer builds only what the import uses, the simplifier and the
# EXT_meshopt_compression decoders. Both are MIT submodules under third_party/, unmodified.
set(GEODE_MESHOPT_DIR ${CMAKE_SOURCE_DIR}/third_party/meshoptimizer/src)
add_library(geode_meshopt STATIC
    ${GEODE_MESHOPT_DIR}/simplifier.cpp
    ${GEODE_MESHOPT_DIR}/vertexcodec.cpp
    ${GEODE_MESHOPT_DIR}/indexcodec.cpp
    ${GEODE_MESHOPT_DIR}/vertexfilter.cpp)
target_include_directories(geode_meshopt PUBLIC ${GEODE_MESHOPT_DIR})
target_compile_options(geode_meshopt PRIVATE -fvisibility=hidden $<$<CONFIG:Release>:-O2>)
target_sources(geode_core PRIVATE scene/object/GltfImport.cpp)
target_include_directories(geode_core PRIVATE ${CMAKE_SOURCE_DIR}/third_party/cgltf)
target_link_libraries(geode_core PUBLIC geode_meshopt)

# The imported model's textures (core/scene/object/ModelTextures, #474): Basis Universal's encoder
# library as upstream lists it (ENCODER_LIB_SRC_LIST in its CMakeLists.txt, minus the optional
# astcenc), for its PNG and JPEG readers, the UASTC encoder and the transcoder (KTX2, UASTC -> ASTC
# and ETC2), plus zstd for UASTC KTX2 files that are zstd-supercompressed. Apache-2.0 (zstd BSD),
# an unmodified submodule. Built as C++17, upstream's standard; no SSE, no OpenCL, and the job pool
# it compiles is never started (the import is single-threaded on its caller's worker).
set(GEODE_BASISU_DIR ${CMAKE_SOURCE_DIR}/third_party/basis_universal)
add_library(geode_basisu STATIC
    ${GEODE_BASISU_DIR}/encoder/basisu_backend.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_basis_file.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_comp.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_enc.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_etc.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_frontend.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_gpu_texture.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_pvrtc1_4.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_resampler.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_resample_filters.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_ssim.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_uastc_enc.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_bc7e_scalar.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_dds_export.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_bc7enc.cpp
    ${GEODE_BASISU_DIR}/encoder/jpgd.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_kernels_sse.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_bc15_spmd.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_bc15_spmd_sse.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_opencl.cpp
    ${GEODE_BASISU_DIR}/encoder/pvpngreader.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_uastc_hdr_4x4_enc.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_astc_hdr_6x6_enc.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_astc_hdr_common.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_astc_ldr_common.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_astc_ldr_encode.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_astc_ldr_fencode.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_xbc7_encode.cpp
    ${GEODE_BASISU_DIR}/encoder/basisu_tinyexr.cpp
    ${GEODE_BASISU_DIR}/encoder/3rdparty/android_astc_decomp.cpp
    ${GEODE_BASISU_DIR}/transcoder/basisu_transcoder.cpp
    ${GEODE_BASISU_DIR}/zstd/zstd.c)
set_target_properties(geode_basisu PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)
target_include_directories(geode_basisu PUBLIC ${GEODE_BASISU_DIR})
target_compile_definitions(geode_basisu PUBLIC
    BASISU_SUPPORT_SSE=0 BASISU_SUPPORT_OPENCL=0 BASISD_SUPPORT_KTX2=1 BASISD_SUPPORT_KTX2_ZSTD=1)
target_compile_definitions(geode_basisu PRIVATE BASISU_SUPPORT_ASTCENC=0)
target_compile_options(geode_basisu PRIVATE -fvisibility=hidden -fno-strict-aliasing -w $<$<CONFIG:Release>:-O2>)
target_sources(geode_core PRIVATE scene/object/ModelTextures.cpp)
target_link_libraries(geode_core PUBLIC geode_basisu)

# Show on a computer over WebRTC (cast/WebRtcLink, owner 2026-11-12): libdatachannel (MPL-2.0) and its Mbed TLS
# (Apache-2.0), configured by the root CMakeLists.txt. cast/ is the only native directory that may include them
# or any socket header (tools/network-gate.sh).
target_sources(geode_core PRIVATE
    cast/WebRtcLink.cpp
    api/geode_webrtc_api.cpp)
target_link_libraries(geode_core PRIVATE LibDataChannel::LibDataChannelStatic)
