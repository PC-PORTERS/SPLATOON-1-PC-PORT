cmake_minimum_required(VERSION 3.18)
project(GambitPort CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/gx2_windowing.cmake)
if(DEFINED RBREW_GRAPHICS_BACKEND AND NOT RBREW_GRAPHICS_BACKEND STREQUAL "Vulkan")
    message(FATAL_ERROR "Gambit requires the Vulkan backend. No alternate graphics backend is available.")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/gx2_graphics_backend.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/imgui.cmake)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
add_subdirectory(third_party/cemu_shader)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/miniaudio.h")
    message(STATUS "miniaudio.h found — audio back-end enabled (HAVE_MINIAUDIO)")
    set(HAVE_MINIAUDIO ON)
else()
    message(STATUS "miniaudio.h not found — audio output disabled (silent stubs)")
    set(HAVE_MINIAUDIO OFF)
endif()

add_library(gambit_runtime STATIC
    runtime/rebrewu_runtime.cpp
    runtime/rebrewu_paired.cpp
    os/gambit_plt_auto.cpp
    os/coreinit/coreinit_memory.cpp
    os/coreinit/coreinit_threads.cpp
    os/coreinit/coreinit_sync.cpp
    os/coreinit/coreinit_misc.cpp
    os/coreinit/coreinit_shared_data.cpp
    os/coreinit/coreinit_dynload.cpp
    os/coreinit/coreinit_overlay.cpp
    os/coreinit/coreinit_system_settings.cpp
    os/coreinit/coreinit_user_config.cpp
    os/erreula/erreula.cpp
    os/nn_act/nn_act.cpp
    os/sysapp/sysapp_title.cpp
    os/coreinit/coreinit_fs.cpp
    os/gx2/gx2_context.cpp
    os/gx2/gx2_draw.cpp
    os/gx2/gx2_surface_layout.cpp
    os/gx2/r700_to_glsl.cpp
    os/gx2/gx2_shader_program.cpp
    os/gx2/gx2_pm4_state.cpp
    os/gx2/gx2_display_list.cpp
    os/gx2/gx2_detile.cpp
    os/gx2/gx2_write_watch.cpp
    os/gx2/gx2_fetch_decode.cpp
    os/snd_core/snd_core.cpp
    os/vpad/game_controllers.cpp
    os/vpad/vpad.cpp
    os/padscore/padscore.cpp
    os/stubs/stubs.cpp
    os/nsysnet/nsysnet.cpp
    os/game/player_anim.cpp
    loader/gambit_loader.cpp
    loader/native_console_profile.cpp
    runtime/splatoon_diagnostics.cpp
    runtime/discord_rpc.cpp
    runtime/port_settings.cpp
)

target_include_directories(gambit_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(gambit_runtime PUBLIC HAVE_SDL2)
# Gameplay builds contain no console/file diagnostics or profiler hooks.
# RBREW_DIAGNOSTIC_RUNTIME keeps them for a separately linked, one-off
# diagnostic copy (capture/trace); never for the delivered game.
option(RBREW_DIAGNOSTIC_RUNTIME "Keep diagnostics in the runtime for a one-off diagnostic copy" OFF)
if(NOT RBREW_DIAGNOSTIC_RUNTIME)
    target_compile_definitions(gambit_runtime PRIVATE RBREW_DISABLE_LOGGING)
    target_compile_definitions(cemu_shader PRIVATE RBREW_DISABLE_LOGGING)
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS RBREW_DISABLE_LOGGING)
endif()
rbrew_gx2_backend(gambit_runtime PUBLIC)

if(HAVE_MINIAUDIO)
    target_compile_definitions(gambit_runtime PUBLIC HAVE_MINIAUDIO)
    target_include_directories(gambit_runtime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
    )
endif()

target_link_libraries(gambit_runtime PUBLIC
    cemu_shader
    ${SDL2_LIBRARIES}

    CURL::libcurl
    ZLIB::ZLIB
)
# Dear ImGui (docking branch, fetched at configure time): the performance
# overlay backend. PRIVATE: only the GX2 Vulkan layer uses it.
target_link_libraries(gambit_runtime PRIVATE imgui)

if(WIN32)
    target_link_libraries(gambit_runtime PUBLIC ws2_32 winmm)
    if(HAVE_MINIAUDIO)
        target_link_libraries(gambit_runtime PUBLIC ole32 ksuser)
    endif()
endif()

if(UNIX AND NOT APPLE AND HAVE_MINIAUDIO)
    find_library(ALSA_LIB asound)
    if(ALSA_LIB)
        target_link_libraries(gambit_runtime PUBLIC ${ALSA_LIB})
    endif()
    find_library(PTHREAD_LIB pthread)
    if(PTHREAD_LIB)
        target_link_libraries(gambit_runtime PUBLIC ${PTHREAD_LIB})
    endif()
endif()

set(GAMBIT_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Gambit" CACHE PATH
    "Directory containing RebrewU-generated Gambit C++")
set(RBREW_GAME_DEFAULT OFF)
if(EXISTS "${GAMBIT_GENERATED_DIR}/Gambit_data.cpp")
    set(RBREW_GAME_DEFAULT ON)
endif()
option(RBREW_BUILD_GAME "Build a private, locally generated game" ${RBREW_GAME_DEFAULT})
if(RBREW_BUILD_GAME)
file(GLOB GAMBIT_PARTS "${GAMBIT_GENERATED_DIR}/Gambit_part*.cpp")

add_library(gambit_generated OBJECT
    ${GAMBIT_PARTS}
    ${GAMBIT_GENERATED_DIR}/Gambit_data.cpp
    ${GAMBIT_GENERATED_DIR}/Gambit_register.cpp
)

target_include_directories(gambit_generated PRIVATE
    ${GAMBIT_GENERATED_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(gambit_generated PRIVATE
    RBREW_DISABLE_LOGGING
    HAVE_SDL2
)

if(MSVC)
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        target_compile_options(gambit_generated PRIVATE /MP)
    endif()
    target_compile_options(gambit_generated PRIVATE "$<$<CONFIG:Debug>:/Od>" /bigobj /Ob1 /GS- /Gy- /EHs-c- /GR-)
    if(EXISTS "${GAMBIT_GENERATED_DIR}/Gambit.h")
        target_precompile_headers(gambit_generated PRIVATE "${GAMBIT_GENERATED_DIR}/Gambit.h")
    endif()
endif()

add_executable(Gambit
    main.cpp
)

target_include_directories(Gambit PRIVATE
    ${GAMBIT_GENERATED_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(Gambit PRIVATE gambit_generated gambit_runtime)
set_target_properties(Gambit PROPERTIES OUTPUT_NAME Splatoon)
endif()
if(WIN32)
    enable_language(RC)
    if(TARGET Gambit)
        target_sources(Gambit PRIVATE resources/splatoon.rc)
        target_include_directories(Gambit PRIVATE resources)
        if(MSVC)
            # splatoon.rc already embeds the application manifest as ID 1.
            # An automatically generated manifest would duplicate that resource.
            target_link_options(Gambit PRIVATE /MANIFEST:NO)
        endif()
    endif()
    add_library(splport_host SHARED EXCLUDE_FROM_ALL main.cpp runtime/splport_host.def)
    target_compile_definitions(splport_host PRIVATE RBREW_DISTRIBUTABLE_RUNTIME RBREW_DISABLE_LOGGING)
    target_link_libraries(splport_host PRIVATE gambit_runtime)
    if(MSVC)
        target_compile_options(splport_host PRIVATE /utf-8)
    endif()
    add_executable(splport_check_runtime EXCLUDE_FROM_ALL tools/check_prerequisites.cpp)
    target_link_libraries(splport_check_runtime PRIVATE Vulkan::Headers volk::volk)
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/runtime/host_process.cmake)
if(TARGET Gambit)
    rbrew_windowed_executable(Gambit)
    if(NOT RBREW_DIAGNOSTIC_RUNTIME)
        target_compile_definitions(Gambit PRIVATE RBREW_DISABLE_LOGGING)
    endif()
endif()

set(RBREW_TITLE_METADATA "" CACHE FILEPATH "Owned title meta.xml used for native MCP region profile")
if(RBREW_TITLE_METADATA)
    if(NOT EXISTS "${RBREW_TITLE_METADATA}")
        message(FATAL_ERROR "Configured RBREW_TITLE_METADATA does not exist")
    endif()
    set_source_files_properties(main.cpp PROPERTIES
        COMPILE_DEFINITIONS RBREW_TITLE_METADATA_PATH="${RBREW_TITLE_METADATA}")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(TARGET Gambit)
        target_compile_options(Gambit PRIVATE "$<$<CONFIG:Debug>:-O0>" -fno-strict-aliasing)
    endif()
    target_compile_options(gambit_runtime PRIVATE -O2)
    if(TARGET Gambit AND CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(Gambit PRIVATE -s)
    endif()
endif()

if(TARGET Gambit)
    set_target_properties(Gambit PROPERTIES UNITY_BUILD OFF)
endif()

if(MSVC)
    # Keep large generated functions unoptimized in diagnostic builds, but
    # leave Release free to use its configuration's /O2 settings.
    if(TARGET Gambit)
        target_compile_options(Gambit PRIVATE "$<$<CONFIG:Debug>:/Od>" /bigobj /MP4)
        # Guest stores read a constant-initialized static TLS flag
        # (rebrewu_runtime.h); the executable needs no per-access TLS guard.
        target_compile_options(Gambit PRIVATE /Zc:tlsGuards-)
        target_link_options(Gambit PRIVATE "/MAP:$<TARGET_FILE_DIR:Gambit>/Splatoon.map")
    endif()
    target_compile_options(gambit_runtime PRIVATE /utf-8 /MP)
endif()

# Optimize host-runtime boundaries without recompiling the large generated
# guest sources with /GL. Consumers link the resulting runtime through LTCG.
# Keep precise floating-point semantics and the existing guest build flags.
option(RBREW_RUNTIME_IPO "Enable MSVC Release interprocedural optimization of the host runtime" ON)
if(RBREW_RUNTIME_IPO AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT RBREW_IPO_SUPPORTED OUTPUT RBREW_IPO_ERROR LANGUAGES CXX)
    if(RBREW_IPO_SUPPORTED)
        set_property(TARGET gambit_runtime PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET cemu_shader PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        target_link_options(gambit_runtime INTERFACE "$<$<CONFIG:Release>:/LTCG>")
    else()
        message(STATUS "Host runtime IPO unavailable: ${RBREW_IPO_ERROR}")
    endif()
endif()

option(RBREW_ENABLE_COVERAGE "Record original guest execution to RBREW_COVERAGE_FILE" OFF)
if(RBREW_ENABLE_COVERAGE)
    target_compile_definitions(gambit_runtime PUBLIC RBREW_ENABLE_COVERAGE)
endif()

option(RBREW_ENABLE_PPC_FALLBACK "Enable conservative hybrid prototype (runtime opt-in too)" OFF)
if(RBREW_ENABLE_PPC_FALLBACK)
    if(NOT TARGET Gambit)
        message(FATAL_ERROR "Hybrid diagnostics require a private generated game")
    endif()
    if(NOT EXISTS "${GAMBIT_GENERATED_DIR}/Gambit_ppc_image.cpp" OR
       NOT EXISTS "${GAMBIT_GENERATED_DIR}/Gambit_ppc_image.bin")
        message(FATAL_ERROR "Hybrid prototype requires regeneration with --emit-ppc-image")
    endif()
    target_sources(gambit_runtime PRIVATE runtime/rebrewu_ppc_interpreter.cpp
        runtime/rebrewu_ppc_hybrid.cpp)
    target_sources(Gambit PRIVATE "${GAMBIT_GENERATED_DIR}/Gambit_ppc_image.cpp")
    target_compile_definitions(gambit_runtime PUBLIC RBREW_ENABLE_PPC_FALLBACK)
    target_compile_definitions(Gambit PRIVATE GAMBIT_PPC_IMAGE_PATH="${GAMBIT_GENERATED_DIR}/Gambit_ppc_image.bin")
endif()

# Offline validation of the actual silent gameplay runtime; no guest code is linked.
option(RBREW_BUILD_TESTS "Build Nintendo-free native performance regression tests" OFF)
if(RBREW_BUILD_TESTS)
    enable_testing()
    # Local developer tests/tools below are optional (not in the public tree).
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_frame_pacing.cpp")
        add_executable(gambit_frame_pacing_tests ../tests/test_frame_pacing.cpp)
        target_include_directories(gambit_frame_pacing_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME native_frame_pacing COMMAND gambit_frame_pacing_tests)
    endif()
    add_executable(gambit_texture_bench ../tests/test_texture_streaming.cpp)
    target_link_libraries(gambit_texture_bench PRIVATE gambit_runtime)
    target_include_directories(gambit_texture_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(WIN32)
        target_link_libraries(gambit_texture_bench PRIVATE psapi)
    endif()
    foreach(case layouts uniforms textures edgecases)
        add_test(NAME streaming_${case} COMMAND gambit_texture_bench ${case})
        set_tests_properties(streaming_${case} PROPERTIES TIMEOUT 120)
    endforeach()
    add_executable(gambit_render_bench ../tests/test_render_performance.cpp)
    target_link_libraries(gambit_render_bench PRIVATE gambit_runtime)
    target_include_directories(gambit_render_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(WIN32)
        target_link_libraries(gambit_render_bench PRIVATE psapi)
    endif()
    if(MSVC)
        # Same authored workloads with an opt-in main-thread sampler
        # (RBREW_BENCH_SAMPLE=1) and a PDB for function names. Not a test.
        add_executable(gambit_render_profile ../tests/test_render_performance.cpp)
        target_compile_definitions(gambit_render_profile PRIVATE RBREW_BENCH_SAMPLER=1)
        target_link_libraries(gambit_render_profile PRIVATE gambit_runtime psapi dbghelp)
        target_include_directories(gambit_render_profile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_options(gambit_render_profile PRIVATE /DEBUG)
    endif()
    foreach(case fallback push churn contracts lifetime heavy heavy_push overdraw watch geometry packets plaza npc_1 npc_10 npc_25 npc_50 npc_100 npc_fallback_50 guest_hooks idle_lookaround present_capture)
        add_test(NAME render_${case} COMMAND gambit_render_bench ${case})
        set_tests_properties(render_${case} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    endforeach()
    # Standalone authored workloads only; never links the generated game.
    add_executable(gambit_shader_bench ../tests/test_shader_performance.cpp)
    target_link_libraries(gambit_shader_bench PRIVATE gambit_runtime)
    target_include_directories(gambit_shader_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(WIN32)
        target_link_libraries(gambit_shader_bench PRIVATE psapi)
    endif()
    foreach(case compiler translation scheduling engine gpu gpu_memory)
        add_test(NAME shader_${case} COMMAND gambit_shader_bench ${case})
        set_tests_properties(shader_${case} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    endforeach()
    target_compile_definitions(gambit_runtime PRIVATE RBREW_ADVANCED_TESTS=1)
    add_executable(gambit_advanced_tests ../tests/test_advanced_performance.cpp)
    target_link_libraries(gambit_advanced_tests PRIVATE gambit_runtime)
    target_include_directories(gambit_advanced_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    foreach(case paired synchronization pipeline_lookup pipeline_gpu)
        add_test(NAME advanced_${case} COMMAND gambit_advanced_tests ${case})
        set_tests_properties(advanced_${case} PROPERTIES TIMEOUT 60)
    endforeach()
    # Guest-CPU character animation in recompiled form (authored code only).
    add_executable(gambit_guest_bench ../tests/test_guest_animation.cpp)
    target_link_libraries(gambit_guest_bench PRIVATE gambit_runtime)
    target_include_directories(gambit_guest_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(gambit_guest_bench PRIVATE /Zc:tlsGuards-) # as the Gambit target
    endif()
    foreach(case npc_1 npc_10 npc_25 npc_50 npc_100 paired_equivalence)
        add_test(NAME guest_${case} COMMAND gambit_guest_bench ${case})
        set_tests_properties(guest_${case} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    endforeach()
    add_executable(gambit_memory_tests ../tests/test_vulkan_memory.cpp)
    target_link_libraries(gambit_memory_tests PRIVATE gambit_runtime)
    target_include_directories(gambit_memory_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    foreach(case policy allocator transfers)
        add_test(NAME memory_${case} COMMAND gambit_memory_tests ${case})
        set_tests_properties(memory_${case} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    endforeach()
    add_test(NAME memory_transfers_serial COMMAND gambit_memory_tests transfers)
    set_tests_properties(memory_transfers_serial PROPERTIES ENVIRONMENT "RBREW_RENDER_SPLIT=0" TIMEOUT 60 SKIP_RETURN_CODE 77)
    add_executable(gambit_resolution_tests ../tests/test_render_resolution.cpp)
    target_link_libraries(gambit_resolution_tests PRIVATE gambit_runtime)
    target_include_directories(gambit_resolution_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME native_display_resolution COMMAND gambit_resolution_tests)
    set_tests_properties(native_display_resolution PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    add_executable(gambit_performance_tests ../tests/test_runtime_performance.cpp)
    target_link_libraries(gambit_performance_tests PRIVATE gambit_runtime)
    target_include_directories(gambit_performance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(gambit_performance_tests PRIVATE /utf-8)
    endif()
    foreach(case timing uploads textures queue queries detile compiler)
        add_test(NAME native_${case} COMMAND gambit_performance_tests ${case})
        set_tests_properties(native_${case} PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
    endforeach()
    foreach(case uploads textures)
        add_test(NAME native_${case}_split COMMAND gambit_performance_tests ${case})
        set_tests_properties(native_${case}_split PROPERTIES ENVIRONMENT "RBREW_RENDER_SPLIT=1" TIMEOUT 30 SKIP_RETURN_CODE 77)
        add_test(NAME native_${case}_serial COMMAND gambit_performance_tests ${case})
        set_tests_properties(native_${case}_serial PROPERTIES ENVIRONMENT "RBREW_RENDER_SPLIT=0" TIMEOUT 30 SKIP_RETURN_CODE 77)
    endforeach()
    add_executable(gambit_efficiency_tests ../tests/test_runtime_efficiency.cpp)
    target_link_libraries(gambit_efficiency_tests PRIVATE gambit_runtime)
    target_include_directories(gambit_efficiency_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(gambit_efficiency_tests PRIVATE /utf-8)
    endif()
    foreach(case detile shader cache_io queue)
        add_test(NAME efficiency_${case} COMMAND gambit_efficiency_tests ${case})
        set_tests_properties(efficiency_${case} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    endforeach()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/splport_analyze.cpp")
        add_executable(splport_analyze tools/splport_analyze.cpp)
        target_link_libraries(splport_analyze PRIVATE gambit_runtime)
        target_include_directories(splport_analyze PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        if(MSVC)
            target_compile_options(splport_analyze PRIVATE /utf-8)
        endif()
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_compatibility.cpp")
        add_executable(gambit_compatibility_tests ../tests/test_compatibility.cpp)
        target_link_libraries(gambit_compatibility_tests PRIVATE gambit_runtime)
        target_include_directories(gambit_compatibility_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        if(MSVC)
            target_compile_options(gambit_compatibility_tests PRIVATE /utf-8)
        endif()
        foreach(case hiz aux account network padscore)
            add_test(NAME compatibility_${case} COMMAND gambit_compatibility_tests ${case})
            set_tests_properties(compatibility_${case} PROPERTIES TIMEOUT 30)
        endforeach()
    endif()
endif()

# The developer test sources are not part of the public tree.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../tests/gx2_replay.cpp")
    add_executable(gx2_replay_quiet EXCLUDE_FROM_ALL ../tests/gx2_replay.cpp)
    target_include_directories(gx2_replay_quiet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/os/gx2)
    target_link_libraries(gx2_replay_quiet PRIVATE gambit_runtime)
    target_compile_definitions(gx2_replay_quiet PRIVATE RBREW_DISABLE_LOGGING)
endif()
# The cache-wide release /MAP names Gambit.map; give this tool its own map so
# building it never replaces the game's map (the last /MAP option wins).
if(MSVC AND TARGET gx2_replay_quiet)
    target_link_options(gx2_replay_quiet PRIVATE "/MAP:$<TARGET_FILE_DIR:gx2_replay_quiet>/gx2_replay_quiet.map")
endif()
