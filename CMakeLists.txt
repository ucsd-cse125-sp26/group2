cmake_minimum_required(VERSION 3.25)

project(group2
    VERSION 0.1.0
    DESCRIPTION "CSE 125 Group 2"
    LANGUAGES CXX
)

# ---- Standard settings ----
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json for clang-tidy / IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerWarnings.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Sanitizers.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Doxygen.cmake")

# ---- Options ----
option(ENABLE_ASAN  "Enable AddressSanitizer"                           OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer"                 OFF)
option(ENABLE_TSAN  "Enable ThreadSanitizer"                            OFF)

# ---- Dependencies ----

# SDL3 — window, input, GPU pipeline (Vulkan/Metal/DX12) + OpenGL context
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.0
    GIT_SHALLOW    TRUE
)
# Build SDL3 as a static library so no DLL shipping is needed at runtime.
set(SDL_SHARED   OFF CACHE BOOL "" FORCE)
set(SDL_STATIC   ON  CACHE BOOL "" FORCE)
# SDL3 3.2.0 has a type mismatch vs newer PipeWire headers; disable it —
# PulseAudio / ALSA remain available on Linux.
set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# SDL3_net — networking library for SDL3
block()
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(
        SDL3_net
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_net.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL3_net)
endblock()

# GLM — header-only math library (vectors, matrices, quaternions)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# EnTT — fast header-only ECS library
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.14.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(EnTT)

# Assimp — 3D model loading (GLTF/GLB, OBJ, FBX, …; client only).
# Keep the build lean: static lib, no install targets, no tools, no tests,
# and only the GLTF/GLB importer enabled.
set(ASSIMP_BUILD_TESTS                   OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL                       OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS            OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT                     ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER           ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER            ON  CACHE BOOL "" FORCE)
# Use Assimp's own bundled minizip — avoids needing system unzip.h in a
# non-standard include path (e.g. /usr/include/minizip/ on Ubuntu 24.04).
set(ASSIMP_BUILD_MINIZIP                 ON  CACHE BOOL "" FORCE)
# Don't let Assimp turn its own -Wall warnings into -Werror; newer Clang
# emits -Wnontrivial-memcall on Assimp internals that we cannot patch.
set(ASSIMP_WARNINGS_AS_ERRORS            OFF CACHE BOOL "" FORCE)
block()
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(
        assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG        v5.4.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(assimp)
endblock()

# stb — single-header image decoding (used by ModelLoader to decompress the
# PNG/JPEG textures embedded in GLB files).  stb ships no CMakeLists.txt, so
# FetchContent just populates the source directory; we expose it as a simple
# INTERFACE library so callers get -isystem suppression.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)
add_library(stb_image INTERFACE)
target_include_directories(stb_image SYSTEM INTERFACE ${stb_SOURCE_DIR})

# ozz-animation — runtime skeletal animation (CPU skinning, sampling, blending).
# We only need the runtime + offline builders; disable all extras.
set(ozz_build_tests     OFF CACHE BOOL "" FORCE)
set(ozz_build_samples   OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos    OFF CACHE BOOL "" FORCE)
set(ozz_build_tools     OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx       OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf      OFF CACHE BOOL "" FORCE)
set(ozz_build_data      OFF CACHE BOOL "" FORCE)
block()
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(
        ozz-animation
        GIT_REPOSITORY https://github.com/guillaumeblanc/ozz-animation.git
        GIT_TAG        0.15.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ozz-animation)
endblock()

# Suppress warnings that fire when our code includes Assimp headers.
# Using SYSTEM marks those include paths as "system" so -Wall / -Wextra
# diagnostics are silenced for Assimp's own headers.
if(TARGET assimp)
    get_target_property(_assimp_inc assimp INTERFACE_INCLUDE_DIRECTORIES)
    if(_assimp_inc)
        target_include_directories(assimp SYSTEM INTERFACE ${_assimp_inc})
    endif()
endif()

# Suppress warnings from ozz-animation headers and its own compilation.
foreach(_ozz_target ozz_base ozz_animation ozz_animation_offline)
    if(TARGET ${_ozz_target})
        target_compile_options(${_ozz_target} PRIVATE
            $<$<CXX_COMPILER_ID:Clang,GNU>:-w>
            $<$<CXX_COMPILER_ID:MSVC>:/W0>
        )
        get_target_property(_ozz_inc ${_ozz_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_ozz_inc)
            target_include_directories(${_ozz_target} SYSTEM INTERFACE ${_ozz_inc})
        endif()
    endif()
endforeach()

# ImGui — immediate-mode debug UI (client only).
# ImGui ships no CMakeLists.txt, so FetchContent_MakeAvailable just downloads
# the source tree without calling add_subdirectory. We compile it ourselves
# as imgui_lib below.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL3::SDL3-static)
# Suppress warnings coming from ImGui's own code — not our problem to fix.
target_compile_options(imgui_lib PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-w>
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
)

# ---- Shaders: GLSL → SPIR-V (→ MSL on macOS) ----
# Step 1: GLSL → SPIR-V
#   Prefers glslc (shaderc); falls back to glslangValidator.
#   Linux:   sudo apt-get install glslang-tools
#   macOS:   brew install glslang
#   Windows: install Vulkan SDK (https://vulkan.lunarg.com/)
# Step 2: SPIR-V → MSL (macOS Metal backend only)
#   macOS:   brew install spirv-cross
#   Linux:   sudo apt-get install spirv-cross  (only needed for cross-builds targeting macOS)
#   Windows: bundled with the Vulkan SDK
find_program(GLSLC glslc
    HINTS ENV VULKAN_SDK
    PATHS /opt/homebrew/bin /usr/local/bin
    PATH_SUFFIXES bin)
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS ENV VULKAN_SDK
    PATHS /opt/homebrew/bin /usr/local/bin
    PATH_SUFFIXES bin)
find_program(SPIRV_CROSS spirv-cross
    HINTS ENV VULKAN_SDK
    PATHS /opt/homebrew/bin /usr/local/bin
    PATH_SUFFIXES bin)

if(GLSLC)
    message(STATUS "Shader compiler: glslc (${GLSLC})")
    set(SHADER_COMPILE_TOOL  "${GLSLC}")
    set(SHADER_COMPILE_FLAGS "")        # glslc infers stage from .vert/.frag
elseif(GLSLANG_VALIDATOR)
    message(STATUS "Shader compiler: glslangValidator (${GLSLANG_VALIDATOR})")
    set(SHADER_COMPILE_TOOL  "${GLSLANG_VALIDATOR}")
    set(SHADER_COMPILE_FLAGS "-V")      # -V = output SPIR-V binary
else()
    message(FATAL_ERROR
        "No GLSL→SPIR-V compiler found.\n"
        "  Linux:   sudo apt-get install glslang-tools\n"
        "  macOS:   brew install glslang\n"
        "  Windows: install the Vulkan SDK (https://vulkan.lunarg.com/)")
endif()

if(SPIRV_CROSS)
    message(STATUS "MSL transpiler:  spirv-cross (${SPIRV_CROSS})")
elseif(APPLE)
    message(FATAL_ERROR
        "spirv-cross not found — required on macOS for the SDL3 GPU Metal backend.\n"
        "  brew install spirv-cross")
else()
    message(STATUS "spirv-cross not found — MSL shaders will not be built (non-macOS build)")
endif()

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

set(SHADER_SOURCES
    projective.vert normal.frag         # scene geometry (cube + floor)
    model.vert model.frag               # legacy model shaders (kept for reference)
    geometry.vert geometry.frag          # graphics team Lambertian shaders
    pbr.vert pbr.frag                   # PBR Cook-Torrance (Phase 1)
    shadow.vert shadow.frag             # shadow map depth-only (Phase 2)
    fullscreen.vert tonemap.frag        # HDR tone mapping (Phase 4)
    skybox.vert skybox.frag             # environment skybox (Phase 5)
    brdf_lut.comp                       # IBL BRDF LUT (Phase 6)
    irradiance.comp                     # IBL diffuse irradiance (Phase 6)
    prefilter.comp                      # IBL specular prefilter (Phase 6)
    ssao.comp ssao_blur.comp            # SSAO (Phase 7)
    bloom_downsample.comp bloom_upsample.comp  # Bloom (Phase 8)
    ssr.comp                            # Screen-space reflections (Phase 9)
    volumetric.comp                     # Volumetric lighting (Phase 10)
    motion_vectors.comp taa.comp        # TAA (Phase 11)
    pbr_wboit.frag oit_resolve.frag     # OIT (Phase 12)
    sss.comp                            # Subsurface scattering (Phase 13)
    # Particle system shaders (Phase 14)
    particle_billboard.vert particle_billboard.frag
    tracer.vert tracer.frag
    ribbon.vert ribbon.frag
    smoke.vert smoke.frag
    lightning_arc.vert lightning_arc.frag
    hitscan_beam.vert hitscan_beam.frag
    decal.vert decal.frag
    sdf_text.vert sdf_text.frag
)
set(SHADER_OUTPUTS "")
foreach(SHADER_SRC ${SHADER_SOURCES})
    # GLSL → SPIR-V
    set(SPV_OUT "${SHADER_OUT_DIR}/${SHADER_SRC}.spv")
    add_custom_command(
        OUTPUT  "${SPV_OUT}"
        COMMAND ${SHADER_COMPILE_TOOL} ${SHADER_COMPILE_FLAGS}
                -o "${SPV_OUT}" "${SHADER_SRC_DIR}/${SHADER_SRC}"
        DEPENDS "${SHADER_SRC_DIR}/${SHADER_SRC}"
        COMMENT "Compiling shader ${SHADER_SRC} → SPIR-V"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS "${SPV_OUT}")

    # SPIR-V → MSL (Metal backend on macOS)
    if(SPIRV_CROSS)
        set(MSL_OUT "${SHADER_OUT_DIR}/${SHADER_SRC}.msl")
        add_custom_command(
            OUTPUT  "${MSL_OUT}"
            COMMAND "${SPIRV_CROSS}" --msl "${SPV_OUT}" --output "${MSL_OUT}"
            DEPENDS "${SPV_OUT}"
            COMMENT "Transpiling shader ${SHADER_SRC} → MSL"
            VERBATIM
        )
        list(APPEND SHADER_OUTPUTS "${MSL_OUT}")
    endif()
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})

# ===========================================================================
# Client executable (group2)
# ===========================================================================
add_executable(group2
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/main/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/game/Game.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/game/Game.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/network/Client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/network/Client.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/Renderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/Renderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/Camera.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/Camera.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/ModelLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/ModelLoader.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/ShaderUtils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/ShaderUtils.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/debug/FrameRecorder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/debug/FrameRecorder.hpp"
    # Particle system
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/ParticleSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/ParticleSystem.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/ParticleRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/ParticleRenderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/ParticleTypes.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/ParticlePool.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/ParticleEvents.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/GpuParticleBuffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/GpuParticleBuffer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/TracerEffect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/TracerEffect.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/ImpactEffect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/ImpactEffect.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/RibbonTrail.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/RibbonTrail.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/HitscanEffect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/HitscanEffect.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/SmokeEffect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/SmokeEffect.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/BulletHoleDecal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/BulletHoleDecal.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/ExplosionEffect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/ExplosionEffect.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/sdf/SdfAtlas.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/sdf/SdfAtlas.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/sdf/SdfFont.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/sdf/SdfRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/sdf/SdfRenderer.hpp"
    # Skeletal animation
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/SkinnedModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/SkinnedModel.hpp"
        src/ecs/systems/MovementSystem.hpp
        src/ecs/systems/CollisionSystem.hpp
        src/ecs/components/Position.hpp
        src/ecs/components/Velocity.hpp
        src/ecs/components/CollisionShape.hpp
        src/ecs/components/PlayerState.hpp
        src/ecs/components/InputSnapshot.hpp
        src/ecs/components/PreviousPosition.hpp
        src/ecs/components/LocalPlayer.hpp
        src/ecs/components/Renderable.hpp
        src/ecs/components/Projectile.hpp
        src/ecs/components/WeaponState.hpp
        src/ecs/components/TracerEmitter.hpp
        src/ecs/components/RibbonEmitter.hpp
        src/ecs/components/ParticleEmitterTag.hpp
        src/ecs/physics/PhysicsConstants.hpp
        src/ecs/physics/Movement.hpp
        src/ecs/physics/Movement.cpp
        src/ecs/physics/SweptCollision.hpp
        src/ecs/physics/SweptCollision.cpp
        src/ecs/systems/MovementSystem.hpp
        src/ecs/systems/MovementSystem.cpp
        src/ecs/systems/CollisionSystem.hpp
        src/ecs/systems/CollisionSystem.cpp
        src/client/systems/InputSampleSystem.hpp
        src/client/systems/PredictionSystem.hpp
        src/client/systems/InputSendSystem.hpp
        src/client/systems/ReconciliationSystem.hpp
        src/client/debug/DebugUI.hpp
        src/client/debug/DebugUI.cpp
        src/network/MessageStream.hpp
        src/network/MessageStream.cpp
        src/ecs/components/ClientId.hpp
)

# src/        — for shared headers (ecs/registry/Registry.hpp, etc.)
# src/client/ — for client-local headers (game/Game.hpp, network/Client.hpp, etc.)
target_include_directories(group2 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client"
)

# Compile shaders before linking the binary.
add_dependencies(group2 shaders)

# Copy compiled SPIR-V shaders next to the binary so SDL_GetBasePath() finds them.
add_custom_command(TARGET group2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SHADER_OUT_DIR}"
            "$<TARGET_FILE_DIR:group2>/shaders"
    COMMENT "Copying shaders to binary directory"
    VERBATIM
)

# Tell the renderer which shader formats were compiled.
if(SPIRV_CROSS)
    target_compile_definitions(group2 PRIVATE HAVE_MSL_SHADERS)
endif()

# ---- Client link libraries ----
target_link_libraries(group2 PRIVATE
    SDL3::SDL3-static
    glm::glm
    SDL3_net::SDL3_net
    EnTT::EnTT
    imgui_lib
    assimp
    stb_image
    ozz_animation_offline
    ozz_animation
    ozz_base
)

# Copy assets (models, textures, …) next to the binary so the renderer can
# find them via SDL_GetBasePath().
add_custom_command(TARGET group2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:group2>/assets"
    COMMENT "Copying assets to binary directory"
    VERBATIM
)

# ===========================================================================
# Server executable
# ===========================================================================
add_executable(server
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/main/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/game/ServerGame.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/game/ServerGame.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/network/Server.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/network/Server.hpp"
        src/ecs/systems/MovementSystem.hpp
        src/ecs/systems/CollisionSystem.hpp
        src/ecs/components/Position.hpp
        src/ecs/components/Velocity.hpp
        src/ecs/components/CollisionShape.hpp
        src/ecs/components/PlayerState.hpp
        src/ecs/components/InputSnapshot.hpp
        src/ecs/components/PreviousPosition.hpp
        src/ecs/components/LocalPlayer.hpp
        src/ecs/physics/PhysicsConstants.hpp
        src/ecs/physics/Movement.hpp
        src/ecs/physics/Movement.cpp
        src/ecs/physics/SweptCollision.hpp
        src/ecs/physics/SweptCollision.cpp
        src/ecs/systems/MovementSystem.hpp
        src/ecs/systems/MovementSystem.cpp
        src/ecs/systems/CollisionSystem.hpp
        src/ecs/systems/CollisionSystem.cpp
        src/server/systems/EventQueue.hpp
        src/server/systems/EventQueue.cpp
        src/server/systems/InputReceiveSystem.hpp
        src/server/systems/BroadcastSystem.hpp
        src/network/MessageStream.hpp
        src/network/MessageStream.cpp
        src/ecs/components/ClientId.hpp
)

# src/        — for shared headers (ecs/registry/Registry.hpp, etc.)
# src/server/ — for server-local headers (game/ServerGame.hpp, network/Server.hpp, etc.)
target_include_directories(server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server"
)

# ---- Server link libraries ----
target_link_libraries(server PRIVATE
    SDL3::SDL3-static
    SDL3_net::SDL3_net
    EnTT::EnTT
    glm::glm
)

# ---- Compiler warnings ----
set_compiler_warnings(group2)
set_compiler_warnings(server)

# ---- Sanitizers ----
enable_sanitizers(group2)
enable_sanitizers(server)

# Windows MSVC: copy the ASan runtime DLL next to the binary so it runs
# without a Developer PowerShell (clang_rt.asan_dynamic-x86_64.dll is not on
# the user PATH, but it lives alongside cl.exe in the MSVC tools directory).
if(WIN32 AND ENABLE_ASAN AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH _msvc_bin_dir)
    set(_asan_dll "${_msvc_bin_dir}/clang_rt.asan_dynamic-x86_64.dll")
    if(EXISTS "${_asan_dll}")
        add_custom_command(TARGET group2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_asan_dll}"
                    "$<TARGET_FILE_DIR:group2>/clang_rt.asan_dynamic-x86_64.dll"
            COMMENT "Copying MSVC ASan runtime DLL to output directory"
            VERBATIM
        )
    else()
        message(WARNING
            "MSVC ASan DLL not found: ${_asan_dll} — "
            "the debug binary may fail with 0xC0000135 outside a Developer PowerShell.")
    endif()
endif()

# ---- Windows: suppress min/max macros from <windows.h> ----
if(WIN32)
    target_compile_definitions(group2 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---- Symlink compile_commands.json to project root for clang-tidy ----
if(NOT WIN32)
    add_custom_target(symlink_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMENT "Symlinking compile_commands.json to source root"
        VERBATIM
    )
endif()

# ---- clang-format targets ----
# Prefer clang-format-18 (matches CI / ubuntu-24.04) so local and CI agree exactly.
find_program(CLANG_FORMAT NAMES clang-format-18 clang-format)
if(CLANG_FORMAT)
    execute_process(
        COMMAND ${CLANG_FORMAT} --version
        OUTPUT_VARIABLE _cf_ver
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "clang-format: ${CLANG_FORMAT}  (${_cf_ver})")

    # Collect all project sources (exclude build/_deps)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_SOURCE_DIR}/src/*.h"
    )

    # `cmake --build --preset debug --target format`  — reformat in-place
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format (in-place) on all sources"
        VERBATIM
    )

    # `cmake --build --preset debug --target format-check`  — exit non-zero if unformatted
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCE_FILES}
        COMMENT "Checking clang-format compliance"
        VERBATIM
    )

    # Point git at the committed .githooks/ directory (idempotent, no generated files).
    # This activates pre-commit (auto-format) and pre-push (format gate) for every
    # developer as soon as they run cmake configure — no manual hook installation needed.
    if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
        execute_process(
            COMMAND git config core.hooksPath .githooks
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _hooks_result
        )
        if(_hooks_result EQUAL 0)
            message(STATUS "Git hooks activated → .githooks/ (pre-commit + pre-push)")
        else()
            message(WARNING "Could not set git core.hooksPath — hooks may not run")
        endif()
    endif()
else()
    message(WARNING "clang-format not found — format/format-check targets unavailable. "
                    "Run scripts/setup-linux.sh or scripts/setup-macos.sh.")
endif()
