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
option(GROUP2_ENABLE_VHACD "Build legacy V-HACD decomposition support for prototype props" OFF)

# ---- Build acceleration ----

# ccache — cache compiled object files so rebuilds (even after rm -rf build/)
# return cached results instantly.  Auto-detected; install once per machine:
#   Linux:   sudo apt install ccache
#   macOS:   brew install ccache
#   Windows: choco install ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Compiler cache: ccache (${CCACHE_PROGRAM})")
else()
    message(STATUS "ccache not found — builds will not be cached "
                   "(install for much faster rebuilds)")
endif()

# After the first successful configure, skip git-fetch checks on every
# subsequent re-configure.  This shaves seconds off every cmake invocation
# and allows fully-offline rebuilds.
# To update a dep: cmake --preset debug -DFETCHCONTENT_UPDATES_DISCONNECTED=OFF
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
    "Skip FetchContent git-fetch after first successful populate")

# Use the mold linker on Linux for ~5-10× faster linking.
# Gracefully falls back to the default linker if mold is not installed.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(MOLD_LINKER mold)
    if(MOLD_LINKER)
        set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=mold")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=mold")
        message(STATUS "Linker: mold (${MOLD_LINKER})")
    else()
        message(STATUS "mold not found — using default linker "
                       "(install with: sudo apt install mold)")
    endif()
endif()

# Note: -gsplit-dwarf was previously enabled here for Linux Debug builds, but
# Clang refuses to mix split-dwarf with precompiled headers (the per-file
# "_gsplit_dwarf_path" macro is baked into the PCH and then mismatches every
# consuming TU).  Both SDL3 3.4.x and our own targets use PCH, so the flag is
# off across the board.  PCH already provides the bulk of the rebuild-speed
# win that split-dwarf was helping with.

# Clang validates PCH freshness by checking system header mtime.  Package
# manager updates (apt upgrade, brew upgrade) bump mtimes without changing
# content, causing spurious "precompiled header has been modified" errors.
# -fno-pch-timestamp stops baking file timestamps into the PCH so mtime
# changes alone never invalidate it.  Applied only to our own targets (not
# FetchContent deps) via target_compile_options below — search for
# "PCH_NO_TIMESTAMP_TARGETS".
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(PCH_NO_TIMESTAMP_FLAG "-Xclang;-fno-pch-timestamp" CACHE INTERNAL "")
endif()

# ---- Dependencies ----

set(GROUP2_DEPENDENCIES_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies.json")
file(READ "${GROUP2_DEPENDENCIES_FILE}" GROUP2_DEPENDENCIES_JSON)

function(group2_dependency_git name out_repository out_tag)
    string(JSON _repository ERROR_VARIABLE _repository_error
           GET "${GROUP2_DEPENDENCIES_JSON}" "${name}" repository)
    if(_repository_error)
        message(FATAL_ERROR
            "Missing repository for dependency '${name}' in ${GROUP2_DEPENDENCIES_FILE}: "
            "${_repository_error}")
    endif()

    string(JSON _tag ERROR_VARIABLE _tag_error
           GET "${GROUP2_DEPENDENCIES_JSON}" "${name}" tag)
    if(_tag_error)
        message(FATAL_ERROR
            "Missing tag for dependency '${name}' in ${GROUP2_DEPENDENCIES_FILE}: "
            "${_tag_error}")
    endif()

    set(${out_repository} "${_repository}" PARENT_SCOPE)
    set(${out_tag} "${_tag}" PARENT_SCOPE)
endfunction()

function(group2_dependency_url name out_url)
    string(JSON _url ERROR_VARIABLE _url_error
           GET "${GROUP2_DEPENDENCIES_JSON}" "${name}" url)
    if(_url_error)
        message(FATAL_ERROR
            "Missing URL for dependency '${name}' in ${GROUP2_DEPENDENCIES_FILE}: "
            "${_url_error}")
    endif()

    set(${out_url} "${_url}" PARENT_SCOPE)
endfunction()

# SDL3 — window, input, GPU pipeline (Vulkan/Metal/DX12) + OpenGL context
group2_dependency_git(SDL3 SDL3_REPOSITORY SDL3_TAG)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY "${SDL3_REPOSITORY}"
    GIT_TAG        "${SDL3_TAG}"
    GIT_SHALLOW    TRUE
)
# Build SDL3 as a static library so no DLL shipping is needed at runtime.
set(SDL_SHARED   OFF CACHE BOOL "" FORCE)
set(SDL_STATIC   ON  CACHE BOOL "" FORCE)
# SDL3 has a type mismatch vs newer PipeWire headers; disable it —
# PulseAudio / ALSA remain available on Linux.
set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# SDL3_net — networking library for SDL3
block()
    set(BUILD_SHARED_LIBS OFF)
    group2_dependency_git(SDL3_net SDL3_NET_REPOSITORY SDL3_NET_TAG)
    FetchContent_Declare(
        SDL3_net
        GIT_REPOSITORY "${SDL3_NET_REPOSITORY}"
        GIT_TAG        "${SDL3_NET_TAG}"
    )
    FetchContent_MakeAvailable(SDL3_net)
endblock()

# SDL_shadercross — build-time shader transpiler.
#   SPIR-V → DXIL (Windows / Direct3D 12)
#   SPIR-V → MSL  (macOS  / Metal)
# We only need the `shadercross` CLI executable; the runtime library is not used
# (shaders are transpiled at build time and shipped as pre-compiled blobs).
#
# SDL_shadercross's "vendored" mode builds DXC's full LLVM/Clang stack from
# source (~1700 compile units; also requires the ATL workload on MSVC).  We
# avoid that by fetching DXC's official prebuilt binaries and SPIRV-Cross
# sources separately, then telling SDL_shadercross to use them.
if(WIN32)
    set(SHADERCROSS_PLATFORM_NEEDS_TRANSPILE ON)
elseif(APPLE)
    set(SHADERCROSS_PLATFORM_NEEDS_TRANSPILE ON)
else()
    set(SHADERCROSS_PLATFORM_NEEDS_TRANSPILE OFF)
endif()

if(SHADERCROSS_PLATFORM_NEEDS_TRANSPILE)
    # SPIRV-Cross — ships SDL_shadercross's MSL + reflection backends.
    block()
        set(SPIRV_CROSS_CLI                 OFF CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_ENABLE_TESTS        OFF CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_SHARED              OFF CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_STATIC              ON  CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_ENABLE_C_API        ON  CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_ENABLE_GLSL         ON  CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_ENABLE_HLSL         ON  CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_ENABLE_MSL          ON  CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_ENABLE_REFLECT      ON  CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_ENABLE_CPP          OFF CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_ENABLE_UTIL         ON  CACHE BOOL "" FORCE)
        set(SPIRV_CROSS_SKIP_INSTALL        ON  CACHE BOOL "" FORCE)
        group2_dependency_git(SPIRV-Cross SPIRV_CROSS_REPOSITORY SPIRV_CROSS_TAG)
        FetchContent_Declare(
            SPIRV-Cross
            GIT_REPOSITORY "${SPIRV_CROSS_REPOSITORY}"
            GIT_TAG        "${SPIRV_CROSS_TAG}"
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(SPIRV-Cross)
    endblock()

    # SPIRV-Cross creates targets with hyphens (spirv-cross-c) but
    # SDL_shadercross looks for underscored names (spirv_cross_c) via
    # find_package, gated on `if(NOT TARGET spirv_cross_c)`.  Add aliases so
    # that gate sees the in-tree targets and skips find_package.
    foreach(_v core glsl hlsl msl reflect util c)
        if(TARGET spirv-cross-${_v} AND NOT TARGET spirv_cross_${_v})
            add_library(spirv_cross_${_v} ALIAS spirv-cross-${_v})
        endif()
    endforeach()

    # SDL_shadercross's CMakeLists unconditionally calls
    #   export(TARGETS SDL3_shadercross-static ...)
    # which walks transitive deps and fails if spirv-cross-c isn't part of any
    # export set.  Put the SPIRV-Cross targets we use into a throwaway export
    # so CMake's "in any export set" check passes.
    set(_spvc_export_targets)
    foreach(_t spirv-cross-c spirv-cross-core spirv-cross-glsl spirv-cross-hlsl
                spirv-cross-msl spirv-cross-reflect spirv-cross-util)
        if(TARGET ${_t})
            list(APPEND _spvc_export_targets ${_t})
        endif()
    endforeach()
    if(_spvc_export_targets)
        export(TARGETS ${_spvc_export_targets}
               FILE "${CMAKE_BINARY_DIR}/SPIRV-Cross-vendored-targets.cmake")
    endif()

    # On Windows, fetch Microsoft's prebuilt DXC release zip so SDL_shadercross
    # can find dxcompiler.dll/.lib/dxcapi.h without building LLVM ourselves.
    # On other platforms we disable DXC entirely (we only need MSL on macOS).
    if(WIN32)
        group2_dependency_url(dxc_binaries DXC_BINARIES_URL)
        FetchContent_Declare(
            dxc_binaries
            URL "${DXC_BINARIES_URL}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(dxc_binaries)
        set(DirectXShaderCompiler_ROOT "${dxc_binaries_SOURCE_DIR}" CACHE PATH "" FORCE)

        # Pre-set FindDirectXShaderCompiler.cmake results so find_library() never
        # falls back to the Windows SDK's dxcompiler.lib (which is a stub that
        # lacks the DxcCreateInstance export, causing LNK2019 on some machines).
        set(DirectXShaderCompiler_INCLUDE_PATH  "${dxc_binaries_SOURCE_DIR}/inc"                  CACHE PATH     "" FORCE)
        set(DirectXShaderCompiler_dxcompiler_LIBRARY "${dxc_binaries_SOURCE_DIR}/lib/x64/dxcompiler.lib" CACHE FILEPATH "" FORCE)
        set(DirectXShaderCompiler_dxcompiler_BINARY  "${dxc_binaries_SOURCE_DIR}/bin/x64/dxcompiler.dll" CACHE FILEPATH "" FORCE)
        set(DirectXShaderCompiler_dxil_BINARY        "${dxc_binaries_SOURCE_DIR}/bin/x64/dxil.dll"       CACHE FILEPATH "" FORCE)
        set(SHADERCROSS_DXC_ENABLED ON)
    else()
        set(SHADERCROSS_DXC_ENABLED OFF)
    endif()

    block()
        set(SDLSHADERCROSS_VENDORED         OFF CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_DXC              ${SHADERCROSS_DXC_ENABLED} CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_CLI              ON  CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_SHARED           OFF CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_STATIC           ON  CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_CLI_STATIC       ON  CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_INSTALL          OFF CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_TESTS            OFF CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_WERROR           OFF CACHE BOOL "" FORCE)
        group2_dependency_git(SDL_shadercross SDL_SHADERCROSS_REPOSITORY SDL_SHADERCROSS_TAG)
        FetchContent_Declare(
            SDL_shadercross
            GIT_REPOSITORY "${SDL_SHADERCROSS_REPOSITORY}"
            GIT_TAG        "${SDL_SHADERCROSS_TAG}"
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(SDL_shadercross)
    endblock()
endif()

# glslang — GLSL → SPIR-V front-end.  Built from source so contributors don't
# need a system Vulkan SDK install; produces the `glslang-standalone` CLI
# target (output name: `glslang`) that the shader build step below invokes via
# $<TARGET_FILE:…>.  Pinned to the same Vulkan SDK revision as SPIRV-Cross
# above (line 149) to keep the toolchain consistent.
#
# ENABLE_OPT=OFF avoids pulling SPIRV-Tools (we don't run --spirv-opt; DXC and
# shadercross handle later-stage codegen).  ENABLE_HLSL=OFF — HLSL goes through
# DXC inside shadercross, not glslang.
block()
    set(BUILD_SHARED_LIBS         OFF)
    set(BUILD_TESTING             OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS             OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPT                OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL               OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES   ON  CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER        OFF CACHE BOOL "" FORCE)
    set(ENABLE_CTEST              OFF CACHE BOOL "" FORCE)
    set(SKIP_GLSLANG_INSTALL      ON  CACHE BOOL "" FORCE)
    group2_dependency_git(glslang GLSLANG_REPOSITORY GLSLANG_TAG)
    FetchContent_Declare(
        glslang
        GIT_REPOSITORY "${GLSLANG_REPOSITORY}"
        GIT_TAG        "${GLSLANG_TAG}"
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glslang)
endblock()

# GLM — header-only math library (vectors, matrices, quaternions)
group2_dependency_git(glm GLM_REPOSITORY GLM_TAG)
FetchContent_Declare(
    glm
    GIT_REPOSITORY "${GLM_REPOSITORY}"
    GIT_TAG        "${GLM_TAG}"
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# EnTT — fast header-only ECS library
group2_dependency_git(EnTT ENTT_REPOSITORY ENTT_TAG)
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY "${ENTT_REPOSITORY}"
    GIT_TAG        "${ENTT_TAG}"
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(EnTT)
if(TARGET EnTT)
    get_target_property(_entt_include_dirs EnTT INTERFACE_INCLUDE_DIRECTORIES)
    if(_entt_include_dirs)
        set_target_properties(EnTT PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_entt_include_dirs}"
        )
    endif()
endif()

# toml++ - TOML parsing library
group2_dependency_git(tomlplusplus TOMLPLUSPLUS_REPOSITORY TOMLPLUSPLUS_TAG)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY "${TOMLPLUSPLUS_REPOSITORY}"
    GIT_TAG        "${TOMLPLUSPLUS_TAG}"
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

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
set(ASSIMP_BUILD_OBJ_IMPORTER            ON  CACHE BOOL "" FORCE)
# Use Assimp's own bundled minizip — avoids needing system unzip.h in a
# non-standard include path (e.g. /usr/include/minizip/ on Ubuntu 24.04).
set(ASSIMP_BUILD_MINIZIP                 ON  CACHE BOOL "" FORCE)
# Don't let Assimp turn its own -Wall warnings into -Werror; newer Clang
# emits -Wnontrivial-memcall on Assimp internals that we cannot patch.
set(ASSIMP_WARNINGS_AS_ERRORS            OFF CACHE BOOL "" FORCE)
block()
    set(BUILD_SHARED_LIBS OFF)
    group2_dependency_git(assimp ASSIMP_REPOSITORY ASSIMP_TAG)
    FetchContent_Declare(
        assimp
        GIT_REPOSITORY "${ASSIMP_REPOSITORY}"
        GIT_TAG        "${ASSIMP_TAG}"
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(assimp)
endblock()

# stb — single-header image decoding (used by ModelLoader to decompress the
# PNG/JPEG textures embedded in GLB files).  stb ships no CMakeLists.txt, so
# FetchContent just populates the source directory; we expose it as a simple
# INTERFACE library so callers get -isystem suppression.
group2_dependency_git(stb STB_REPOSITORY STB_TAG)
FetchContent_Declare(
    stb
    GIT_REPOSITORY "${STB_REPOSITORY}"
    GIT_TAG        "${STB_TAG}"
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)
add_library(stb_image INTERFACE)
target_include_directories(stb_image SYSTEM INTERFACE ${stb_SOURCE_DIR})

# LunaSVG — static SVG rasterization for HUD assets. Runtime draws the
# resulting RGBA pixels through the HUD sprite atlas.
block()
    set(BUILD_SHARED_LIBS OFF)
    set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LUNASVG_DISABLE_LOAD_SYSTEM_FONTS ON CACHE BOOL "" FORCE)
    group2_dependency_git(lunasvg LUNASVG_REPOSITORY LUNASVG_TAG)
    FetchContent_Declare(
        lunasvg
        GIT_REPOSITORY "${LUNASVG_REPOSITORY}"
        GIT_TAG        "${LUNASVG_TAG}"
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(lunasvg)
endblock()
foreach(_lunasvg_target lunasvg plutovg)
    if(TARGET ${_lunasvg_target})
        target_compile_options(${_lunasvg_target} PRIVATE
            $<$<CXX_COMPILER_ID:Clang,GNU>:-w>
            $<$<CXX_COMPILER_ID:MSVC>:/W0>
        )
        get_target_property(_lunasvg_inc ${_lunasvg_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_lunasvg_inc)
            target_include_directories(${_lunasvg_target} SYSTEM INTERFACE ${_lunasvg_inc})
        endif()
    endif()
endforeach()

# minimp3 — single-header MP3 decoder (used by SfxSystem to load MP3 sound assets).
# Same FetchContent+INTERFACE pattern as stb above.
group2_dependency_git(minimp3 MINIMP3_REPOSITORY MINIMP3_TAG)
FetchContent_Declare(
    minimp3
    GIT_REPOSITORY "${MINIMP3_REPOSITORY}"
    GIT_TAG        "${MINIMP3_TAG}"
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(minimp3)
add_library(minimp3 INTERFACE)
target_include_directories(minimp3 SYSTEM INTERFACE ${minimp3_SOURCE_DIR})

# Opus — low-latency voice compression for proximity chat.
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
if(WIN32)
    # Opus 1.5.2's CMake files can enable SSE4.1/AVX2 runtime-dispatch
    # sources on Windows without per-file target flags that clang-cl requires.
    # Keep Windows CI portable; proximity voice does not need the SIMD path.
    set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "" FORCE)
    set(OPUS_X86_MAY_HAVE_SSE OFF CACHE BOOL "" FORCE)
    set(OPUS_X86_MAY_HAVE_SSE2 OFF CACHE BOOL "" FORCE)
    set(OPUS_X86_MAY_HAVE_SSE4_1 OFF CACHE BOOL "" FORCE)
    set(OPUS_X86_MAY_HAVE_AVX2 OFF CACHE BOOL "" FORCE)
    set(OPUS_X86_PRESUME_SSE OFF CACHE BOOL "" FORCE)
    set(OPUS_X86_PRESUME_SSE2 OFF CACHE BOOL "" FORCE)
    set(OPUS_X86_PRESUME_SSE4_1 OFF CACHE BOOL "" FORCE)
    set(OPUS_X86_PRESUME_AVX2 OFF CACHE BOOL "" FORCE)
endif()
group2_dependency_git(opus OPUS_REPOSITORY OPUS_TAG)
FetchContent_Declare(
    opus
    GIT_REPOSITORY "${OPUS_REPOSITORY}"
    GIT_TAG        "${OPUS_TAG}"
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(opus)

add_library(vhacd INTERFACE)
if(GROUP2_ENABLE_VHACD)
    # V-HACD — legacy opt-in convex decomposition for prototype props.
    # Production map collision is authored as simplified triangle meshes, so
    # default builds do not fetch or compile V-HACD.
    group2_dependency_git(vhacd_src VHACD_REPOSITORY VHACD_TAG)
    FetchContent_Declare(
        vhacd_src
        GIT_REPOSITORY "${VHACD_REPOSITORY}"
        GIT_TAG        "${VHACD_TAG}"
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(vhacd_src)
    target_include_directories(vhacd SYSTEM INTERFACE ${vhacd_src_SOURCE_DIR}/include)
    target_compile_definitions(vhacd INTERFACE GROUP2_ENABLE_VHACD=1)
endif()

# ozz-animation — runtime skeletal animation (CPU skinning, sampling, blending).
# We only need the runtime + offline builders; disable all extras.
set(ozz_build_tests     OFF CACHE BOOL "" FORCE)
set(ozz_build_samples   OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos    OFF CACHE BOOL "" FORCE)
set(ozz_build_tools     OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx       OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf      OFF CACHE BOOL "" FORCE)
set(ozz_build_data      OFF CACHE BOOL "" FORCE)
set(ozz_build_msvc_rt_dll ON CACHE BOOL "" FORCE)
block()
    set(BUILD_SHARED_LIBS OFF)
    group2_dependency_git(ozz-animation OZZ_ANIMATION_REPOSITORY OZZ_ANIMATION_TAG)
    FetchContent_Declare(
        ozz-animation
        GIT_REPOSITORY "${OZZ_ANIMATION_REPOSITORY}"
        GIT_TAG        "${OZZ_ANIMATION_TAG}"
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
    target_compile_options(assimp PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-nontrivial-memcall>)
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
group2_dependency_git(imgui IMGUI_REPOSITORY IMGUI_TAG)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY "${IMGUI_REPOSITORY}"
    GIT_TAG        "${IMGUI_TAG}"
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
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
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

# argparse -- header-only command-line argument parser used by the server.
group2_dependency_git(argparse ARGPARSE_REPOSITORY ARGPARSE_TAG)
FetchContent_Declare(
    argparse
    GIT_REPOSITORY "${ARGPARSE_REPOSITORY}"
    GIT_TAG        "${ARGPARSE_TAG}"
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(argparse)

# ---- Shaders: GLSL → SPIR-V (→ DXIL on Windows / MSL on macOS) ----
# Step 1: GLSL → SPIR-V
#   Prefers the vendored glslang CLI (target `glslang-standalone`) built from
#   source by the FetchContent block above — no system Vulkan SDK needed.
#   Falls back to system glslc / glslangValidator if a contributor has one
#   from the Vulkan SDK or distro package (saves a couple minutes on first
#   configure for those users).
# Step 2: SPIR-V → DXIL / MSL via SDL_shadercross (the `shadercross` CLI built
#   from the FetchContent block above).  Replaces the legacy spirv-cross +
#   spv_to_msl path.
if(TARGET glslang-standalone)
    message(STATUS "Shader compiler: vendored glslang (target: glslang-standalone)")
    set(SHADER_COMPILE_TOOL  $<TARGET_FILE:glslang-standalone>)
    set(SHADER_COMPILE_FLAGS "-V")       # -V = output SPIR-V binary
    set(SHADER_COMPILE_DEPS  glslang-standalone)
else()
    find_program(GLSLC glslc
        HINTS ENV VULKAN_SDK
        PATHS /opt/homebrew/bin /usr/local/bin
        PATH_SUFFIXES bin)
    find_program(GLSLANG_VALIDATOR glslangValidator
        HINTS ENV VULKAN_SDK
        PATHS /opt/homebrew/bin /usr/local/bin
        PATH_SUFFIXES bin)
    if(GLSLC)
        message(STATUS "Shader compiler: glslc (${GLSLC})")
        set(SHADER_COMPILE_TOOL  "${GLSLC}")
        set(SHADER_COMPILE_FLAGS "")        # glslc infers stage from .vert/.frag
        set(SHADER_COMPILE_DEPS  "")
    elseif(GLSLANG_VALIDATOR)
        message(STATUS "Shader compiler: glslangValidator (${GLSLANG_VALIDATOR})")
        set(SHADER_COMPILE_TOOL  "${GLSLANG_VALIDATOR}")
        set(SHADER_COMPILE_FLAGS "-V")
        set(SHADER_COMPILE_DEPS  "")
    else()
        message(FATAL_ERROR
            "No GLSL→SPIR-V compiler available.  This usually means the\n"
            "vendored glslang FetchContent block failed to expose the\n"
            "`glslang-standalone` target.  Check the configure log above\n"
            "for FetchContent errors, or install a system shader compiler:\n"
            "  Linux:   sudo apt-get install glslang-tools\n"
            "  macOS:   brew install glslang\n"
            "  Windows: install the Vulkan SDK (https://vulkan.lunarg.com/)")
    endif()
endif()

# SDL_shadercross builds a `shadercross` CLI target above (SDLSHADERCROSS_CLI=ON).
# Use that target directly so the shader build stays in lockstep with the
# vendored copy — no system install required.  Only required on platforms that
# transpile SPIR-V → DXIL/MSL (Windows / macOS); Linux runs SPIR-V directly.
if(SHADERCROSS_PLATFORM_NEEDS_TRANSPILE AND NOT TARGET shadercross)
    message(FATAL_ERROR
        "shadercross CLI target not found — SDL_shadercross FetchContent did "
        "not produce the expected target.")
endif()

# Auto-pick transpile targets per platform:
#   Windows → DXIL (primary, Direct3D 12); SPIR-V kept as Vulkan fallback.
#   macOS   → MSL  (Metal); SPIR-V not used at runtime.
#   Linux   → SPIR-V only (Vulkan).
if(WIN32)
    set(SHADER_BUILD_DXIL ON)
    set(SHADER_BUILD_MSL  OFF)
elseif(APPLE)
    set(SHADER_BUILD_DXIL OFF)
    set(SHADER_BUILD_MSL  ON)
else()
    set(SHADER_BUILD_DXIL OFF)
    set(SHADER_BUILD_MSL  OFF)
endif()
set(_shader_targets "SPIR-V")
if(SHADER_BUILD_DXIL)
    string(APPEND _shader_targets ", DXIL")
endif()
if(SHADER_BUILD_MSL)
    string(APPEND _shader_targets ", MSL")
endif()
message(STATUS "Shader targets: ${_shader_targets}")

# Moving-light array size, injected into shaders as a preprocessor define.
# macOS/Metal must stay at 56; every other backend uses 64. Keep this in
# lockstep with MAX_MOVING_POINT_LIGHTS in src/client/renderer-new/Boilerplate.hpp.
if(APPLE)
    set(MAX_MOVING_POINT_LIGHTS 56)
else()
    set(MAX_MOVING_POINT_LIGHTS 64)
endif()
set(SHADER_DEFINE_FLAGS "-DMAX_MOVING_POINT_LIGHTS=${MAX_MOVING_POINT_LIGHTS}")
message(STATUS "Shader MAX_MOVING_POINT_LIGHTS: ${MAX_MOVING_POINT_LIGHTS}")

# Helper: emit a SPIR-V → <target> transpile rule via the shadercross CLI.
# `shadercross` infers the output format from the file extension (.dxil/.msl).
function(_add_transpile_rule SPV_OUT OUT_FILE OUT_LIST_VAR)
    add_custom_command(
        OUTPUT  "${OUT_FILE}"
        COMMAND $<TARGET_FILE:shadercross> "${SPV_OUT}" -o "${OUT_FILE}"
        DEPENDS "${SPV_OUT}" shadercross
        COMMENT "Transpiling ${SPV_OUT} → ${OUT_FILE}"
        VERBATIM
    )
    set(${OUT_LIST_VAR} "${${OUT_LIST_VAR}};${OUT_FILE}" PARENT_SCOPE)
endfunction()

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

set(SHADER_SOURCES
    model.vert model.frag               # legacy model shaders (kept for reference)
    geometry.vert geometry.frag          # graphics team Lambertian shaders
    pbr.vert pbr.frag                   # PBR Cook-Torrance (Phase 1)
    pbr_skinned.vert                    # GPU skinning + instancing (perf Phase 1B)
    shadow_skinned.vert                 # GPU skinning + instancing for shadow pass
    shadow.vert shadow.frag             # shadow map depth-only (Phase 2)
    fullscreen.vert tonemap.frag        # HDR tone mapping (Phase 4)
    skybox.vert skybox.frag             # environment skybox (Phase 5)
    brdf_lut.comp                       # IBL BRDF LUT (Phase 6)
    irradiance.comp                     # IBL diffuse irradiance (Phase 6)
    prefilter.comp                      # IBL specular prefilter (Phase 6)
    gtao.comp gtao_blur.comp            # GTAO (Phase 7)
    bloom_downsample.comp bloom_upsample.comp  # Bloom (Phase 8)
    ssr.comp                            # Screen-space reflections (Phase 9)
    volumetric.comp                     # Volumetric lighting (Phase 10)
    motion_vectors.comp                 # Motion vectors (used by SMAA T2x)
    smaa_fullscreen.vert                # SMAA fullscreen triangle (Y-flipped UVs)
    smaa_edge.frag smaa_blend.frag smaa_neighborhood.frag  # SMAA spatial passes
    smaa_resolve.comp                   # SMAA T2x temporal resolve
    cas.comp                            # CAS (Contrast Adaptive Sharpening)
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
    # HUD overlay
    hud.vert hud.frag
    hud_blit.frag
)
set(SHADER_OUTPUTS "")
foreach(SHADER_SRC ${SHADER_SOURCES})
    # GLSL → SPIR-V
    set(SPV_OUT "${SHADER_OUT_DIR}/${SHADER_SRC}.spv")
    add_custom_command(
        OUTPUT  "${SPV_OUT}"
        COMMAND ${SHADER_COMPILE_TOOL} ${SHADER_COMPILE_FLAGS} ${SHADER_DEFINE_FLAGS}
                -o "${SPV_OUT}" "${SHADER_SRC_DIR}/${SHADER_SRC}"
        DEPENDS "${SHADER_SRC_DIR}/${SHADER_SRC}" ${SHADER_COMPILE_DEPS}
        COMMENT "Compiling shader ${SHADER_SRC} → SPIR-V"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS "${SPV_OUT}")

    # SPIR-V → DXIL (Direct3D 12 backend on Windows)
    if(SHADER_BUILD_DXIL)
        _add_transpile_rule("${SPV_OUT}" "${SHADER_OUT_DIR}/${SHADER_SRC}.dxil" SHADER_OUTPUTS)
    endif()

    # SPIR-V → MSL (Metal backend on macOS)
    if(SHADER_BUILD_MSL)
        _add_transpile_rule("${SPV_OUT}" "${SHADER_OUT_DIR}/${SHADER_SRC}.msl" SHADER_OUTPUTS)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# New-renderer shaders (shaders-new/ directory)
# ---------------------------------------------------------------------------
# Compiled separately so the new renderer can load from "shaders-new/" at
# runtime without colliding with the legacy renderer's shader directory.
set(SHADER_NEW_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders-new")
set(SHADER_NEW_OUT_DIR "${CMAKE_BINARY_DIR}/shaders-new")
file(MAKE_DIRECTORY "${SHADER_NEW_OUT_DIR}")

set(SHADER_NEW_SOURCES
        geometry_depth.vert emtpy.frag
        skinned_geometry.vert skinned_geometry_depth.vert debug.frag chams.frag
        geometry.vert geometry.frag
        geometry_shadowed.frag
        debug_depth.vert debug_depth.frag
        hud.vert hud.frag
        fxaa.frag tonemap.frag
)

foreach(SHADER_SRC ${SHADER_NEW_SOURCES})
    # GLSL → SPIR-V
    set(SPV_OUT "${SHADER_NEW_OUT_DIR}/${SHADER_SRC}.spv")
    add_custom_command(
        OUTPUT  "${SPV_OUT}"
        COMMAND ${SHADER_COMPILE_TOOL} ${SHADER_COMPILE_FLAGS} ${SHADER_DEFINE_FLAGS}
                -o "${SPV_OUT}" "${SHADER_NEW_SRC_DIR}/${SHADER_SRC}"
        DEPENDS "${SHADER_NEW_SRC_DIR}/${SHADER_SRC}" ${SHADER_COMPILE_DEPS}
        COMMENT "Compiling new-renderer shader ${SHADER_SRC} → SPIR-V"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS "${SPV_OUT}")

    if(SHADER_BUILD_DXIL)
        _add_transpile_rule("${SPV_OUT}" "${SHADER_NEW_OUT_DIR}/${SHADER_SRC}.dxil" SHADER_OUTPUTS)
    endif()
    if(SHADER_BUILD_MSL)
        _add_transpile_rule("${SPV_OUT}" "${SHADER_NEW_OUT_DIR}/${SHADER_SRC}.msl" SHADER_OUTPUTS)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DeveloperConfig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DeveloperConfig.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/network/DiscoveryClient.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/network/DiscoveryClient.hpp"
    # PR-11 (server-perf): client-side render-delay interpolation
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/network/EntityInterpolation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/network/EntityInterpolation.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer-new/GraphicsConfig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer-new/GraphicsConfig.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer-new/ShaderUtils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer-new/ShaderUtils.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/debug/FrameRecorder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/debug/FrameRecorder.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/debug/ClientPerfRecorder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/debug/ClientPerfRecorder.hpp"
    # Util
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/util/WorkerPool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/util/WorkerPool.hpp"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/TeslaBeamEffect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/TeslaBeamEffect.hpp"
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
    # Voice chat
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/voice/VoiceCapture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/voice/VoiceCapture.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/voice/VoiceChatSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/voice/VoiceChatSystem.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/voice/VoiceCodec.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/voice/VoiceCodec.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/voice/VoiceJitterBuffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/voice/VoiceJitterBuffer.hpp"
    # Sound effects system
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/AudioMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/AudioMath.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/AudioRuntime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/AudioRuntime.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/SfxSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/SfxSystem.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/SfxEvents.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/SfxTypes.hpp"
    # Skeletal animation
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/FbxImportUtils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/FbxImportUtils.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/CharacterRig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/CharacterRig.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/AnimationLibrary.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/AnimationLibrary.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/AnimationLocomotion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/AnimationLocomotion.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/SkinningBackend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/SkinningBackend.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/SkinVertex.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/CharacterAnimator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/CharacterAnimator.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/AnimationTesterUI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/AnimationTesterUI.hpp"
    # HUD system
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudTypes.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudTween.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudTween.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudRenderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudSvgAtlas.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudSvgAtlas.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudSvgRaster.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudSvgRaster.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudContext.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudContext.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/VoidfallStyle.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudIcons.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudIcons.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/Hud.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/Hud.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/AbilitySelectionWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/AbilitySelectionWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/GrenadeSlotsWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/GrenadeSlotsWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/ChatWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/ChatWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/CrosshairWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/CrosshairWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/RailgunScopeWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/RailgunScopeWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/HealthArmorBar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/HealthArmorBar.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/AmmoCounter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/AmmoCounter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/HitMarkerWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/HitMarkerWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/ShotgunPelletWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/ShotgunPelletWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/KillFeed.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/KillFeed.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/DamageIndicator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/DamageIndicator.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/RoundTimer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/RoundTimer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/TeamStatusBar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/TeamStatusBar.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/Scoreboard.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/Scoreboard.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/EmoteWheelWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/EmoteWheelWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/KillcamBoxWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/KillcamBoxWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/Minimap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/Minimap.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/VignetteWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/VignetteWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/DamageNumberWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/DamageNumberWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/DamageAccumWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/DamageAccumWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/PickupPrompt.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/PickupPrompt.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/PrematchBanner.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/PrematchBanner.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/KdaCounter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/KdaCounter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/EquipmentSlots.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/EquipmentSlots.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/EnemyWorldHealthBar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/EnemyWorldHealthBar.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/PickupNotification.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/PickupNotification.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/PopupNotification.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/PopupNotification.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/GravityIndicator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/GravityIndicator.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/debug/HudDebugPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/debug/HudDebugPanel.hpp"
        src/ecs/systems/MovementSystem.hpp
        src/ecs/systems/CollisionSystem.hpp
        src/ecs/components/Position.hpp
        src/ecs/components/Velocity.hpp
        src/ecs/components/CollisionShape.hpp
        src/ecs/components/PlayerStateEnums.hpp
        src/ecs/components/PlayerVisState.hpp
        src/ecs/components/PlayerSimState.hpp
        src/ecs/components/InputSnapshot.hpp
        src/ecs/components/PreviousPosition.hpp
        src/ecs/components/InterpolationBuffer.hpp
        src/ecs/components/LocalPlayer.hpp
        src/ecs/components/Renderable.hpp
        src/ecs/components/Projectile.hpp
        src/ecs/components/WeaponState.hpp
        src/ecs/components/GrenadeState.hpp
        src/ecs/components/GrenadeConfig.hpp
        src/ecs/components/TracerEmitter.hpp
        src/ecs/components/RibbonEmitter.hpp
        src/ecs/components/ParticleEmitterTag.hpp
        src/ecs/components/AnimatedCharacter.hpp
        src/ecs/physics/PhysicsConstants.hpp
        src/ecs/physics/Movement.hpp
        src/ecs/physics/Movement.cpp
        src/ecs/physics/SweptCollision.hpp
        src/ecs/physics/SweptCollision.cpp
        src/ecs/physics/MapLoader.hpp
        src/ecs/physics/MapLoader.cpp
        src/ecs/physics/TriMeshCollision.hpp
        src/ecs/physics/TriMeshCollision.cpp
        src/ecs/physics/DebugCollisionDraw.hpp
        src/ecs/physics/DebugCollisionDraw.cpp
        src/ecs/physics/PhaseDiagnostic.hpp
        src/ecs/physics/PhaseDiagnostic.cpp
        src/ecs/physics/SurfaceType.hpp
        src/ecs/physics/CollisionEvents.hpp
        src/ecs/physics/CollisionEvents.cpp
        src/ecs/physics/Forces.hpp
        src/ecs/physics/Forces.cpp
        src/ecs/physics/Inertia.hpp
        src/ecs/physics/BroadphaseTree.hpp
        src/ecs/physics/BroadphaseTree.cpp
        src/ecs/physics/ContactManifold.hpp
        src/ecs/physics/ContactCache.hpp
        src/ecs/physics/ContactCache.cpp
        src/ecs/physics/Solver.hpp
        src/ecs/physics/Solver.cpp
        src/ecs/physics/Joints.hpp
        src/ecs/physics/Joints.cpp
        src/ecs/physics/RagdollPbd.hpp
        src/ecs/physics/RagdollPbd.cpp
        src/ecs/physics/Sleep.hpp
        src/ecs/physics/Sleep.cpp
        src/ecs/physics/CookedMeshFormat.hpp
        src/ecs/physics/CookedMeshFormat.cpp
        src/ecs/physics/DeterminismHash.hpp
        src/ecs/physics/DeterminismHash.cpp
        src/ecs/physics/SimdAabb.hpp
        src/ecs/components/RigidBody.hpp
        src/ecs/components/Orientation.hpp
        src/ecs/components/Ragdoll.hpp
        src/ecs/components/TriggerVolume.hpp
        src/ecs/systems/TriggerSystem.hpp
        src/ecs/systems/TriggerSystem.cpp
        src/ecs/systems/RagdollSystem.hpp
        src/ecs/systems/RagdollSystem.cpp
        src/ecs/systems/DynamicsSystem.hpp
        src/ecs/systems/DynamicsSystem.cpp
        src/ecs/physics/VHACDImpl.cpp
        src/ecs/physics/WallDetection.hpp
        src/ecs/physics/WallDetection.cpp
        src/ecs/physics/TitanfallConstants.hpp
        src/ecs/systems/MovementSystem.hpp
        src/ecs/systems/MovementSystem.cpp
        src/ecs/systems/KinematicCharacterController.hpp
        src/ecs/systems/KinematicCharacterController.cpp
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
        src/network/ChatProtocol.hpp
        src/network/ChatProtocol.cpp
        src/network/NetworkConfig.hpp
        src/network/NetworkConfig.cpp
        src/network/VoiceProtocol.hpp
        src/network/VoiceProtocol.cpp
        src/network/RelayToken.hpp
        src/network/crypto/HmacSha256.hpp
        src/network/crypto/HmacSha256.cpp
        src/network/discovery/GlobalDiscoveryProtocol.hpp
        src/network/discovery/GlobalDiscoveryProtocol.cpp
        src/client/network/discovery/GlobalDiscoveryClient.hpp
        src/client/network/discovery/GlobalDiscoveryClient.cpp
        src/network/OutboundQueue.hpp
        src/network/OutboundQueue.cpp
        src/network/transport/FragmentReassembler.hpp
        src/network/transport/PacketHeader.hpp
        src/network/transport/UdpEndpoint.hpp
        src/network/transport/UdpEndpoint.cpp
        src/network/transport/UdpSessionTransport.hpp
        src/network/transport/UdpSessionTransport.cpp
        src/network/RegistryArchive.hpp
        src/network/RegistrySerialization.hpp
        src/network/RegistrySerialization.cpp
        src/ecs/components/ClientId.hpp
        src/client/renderer-new/NewRenderer.cpp
        src/client/renderer-new/NewRenderer.hpp
        src/client/renderer-new/SkinnedRenderer.cpp
        src/client/renderer-new/SkinnedRenderer.hpp
        src/client/renderer-new/Camera.cpp
        src/client/renderer-new/Camera.hpp
        src/client/renderer-new/Asset.hpp
        src/client/renderer-new/AssetLoader.cpp
        src/client/renderer-new/AssetLoader.hpp
        src/client/renderer-new/Boilerplate.cpp
        src/client/renderer-new/Boilerplate.hpp
        src/ecs/systems/WeaponSystem.hpp
        src/ecs/systems/WeaponSystem.hpp
        src/ecs/systems/WeaponSystem.cpp
        src/ecs/systems/WeaponSystem.cpp
        src/ecs/components/WeaponConfig.hpp
        src/ecs/components/WeaponConfig.hpp
        src/ecs/components/GrenadeState.hpp
        src/ecs/components/GrenadeConfig.hpp
        src/ecs/components/ViewmodelConfig.hpp
        src/ecs/components/GripPose.hpp
        src/ecs/components/GripPose.cpp
        src/ecs/systems/WeaponSystem.cpp
        src/network/ShotEvent.hpp
        src/ecs/components/Explosion.hpp
        src/ecs/systems/ExplosionSystem.hpp
        src/ecs/systems/ExplosionSystem.cpp
        src/ecs/components/FireField.hpp
        src/ecs/systems/FireSystem.hpp
        src/ecs/systems/FireSystem.cpp
        src/ecs/systems/PlayerStatusSystem.hpp
        src/ecs/systems/PlayerStatusSystem.cpp
        src/ecs/components/Hitbox.hpp
        src/ecs/systems/HitboxSystem.hpp
        src/ecs/systems/HitboxSystem.cpp
        src/ecs/systems/LagCompensation.hpp
        src/ecs/components/LagCompTarget.hpp
        src/ecs/components/Player.hpp
        src/client/systems/KillFeedEvent.hpp
        src/ecs/components/RespawnTimer.hpp
        src/ecs/components/DeathInfo.hpp
        src/ecs/components/WeaponSpawner.hpp
        src/ecs/components/RespawnPoint.hpp
        src/ecs/components/JumpPad.hpp
        src/ecs/components/Killzone.hpp
        src/ecs/systems/WeaponSpawnerSystem.hpp
        src/ecs/systems/WeaponSpawnerSystem.cpp
        src/ecs/systems/JumpPadSystem.hpp
        src/ecs/systems/JumpPadSystem.cpp
        src/ecs/systems/KillzoneSystem.hpp
        src/ecs/systems/KillzoneSystem.cpp
        src/ecs/systems/DroppedWeaponSystem.hpp
        src/ecs/systems/DroppedWeaponSystem.cpp
        src/ecs/components/AbilityState.hpp
        src/ecs/abilities/Ability.hpp
        src/ecs/abilities/AbilityTuning.hpp
        src/ecs/abilities/GrappleAbility.cpp
        src/ecs/abilities/GrappleAbility.hpp
        src/ecs/abilities/AbilityRegistry.cpp
        src/ecs/abilities/AbilityRegistry.hpp
        src/ecs/systems/AbilitySystem.cpp
        src/ecs/systems/AbilitySystem.hpp
        src/ecs/abilities/DashAbility.cpp
        src/ecs/abilities/DashAbility.hpp
        src/ecs/abilities/GravityAbility.cpp
        src/ecs/abilities/GravityAbility.hpp
        src/ecs/abilities/RecallAbility.cpp
        src/ecs/abilities/RecallAbility.hpp
        src/ecs/abilities/WallhackAbility.cpp
        src/ecs/abilities/WallhackAbility.hpp
        src/client/app/AppContext.hpp
        src/client/app/App.hpp
        src/client/app/App.cpp
        src/client/menus/lobby/Lobby.hpp
        src/client/menus/lobby/Lobby.cpp
        src/client/menus/lobby/ui/LobbyUI.hpp
        src/client/menus/lobby/ui/LobbyUI.cpp
        src/client/renderer-new/RendererTypes.hpp
        src/client/menus/home/Home.hpp
        src/client/menus/home/Home.cpp
        src/client/menus/home/ui/HomeUI.hpp
        src/client/menus/home/ui/HomeUI.cpp
        src/client/menus/MenuTheme.hpp
        src/client/menus/MenuTheme.cpp
        src/ecs/components/PowerupSpawner.hpp
        src/ecs/components/PowerupState.hpp
        src/ecs/systems/PowerupSpawnerSystem.cpp
        src/ecs/systems/PowerupSpawnerSystem.hpp
        src/ecs/systems/PowerupSystem.cpp
        src/ecs/systems/PowerupSystem.hpp
        src/client/config/InputBindings.cpp
        src/client/config/InputBindings.hpp
        src/client/config/UserSettings.cpp
        src/client/config/UserSettings.hpp
        src/client/menus/pause/ConfirmModal.hpp
        src/client/menus/pause/ConfirmModal.cpp
        src/client/menus/pause/PauseMenu.hpp
        src/client/menus/pause/PauseMenu.cpp
        src/client/host/HostedServer.hpp
        src/client/host/HostedServer.cpp
        src/client/menus/host/HostConfig.hpp
        src/client/menus/host/HostConfig.cpp
        src/client/menus/host/ui/HostConfigUI.hpp
        src/client/menus/host/ui/HostConfigUI.cpp
        src/client/util/LocalAddress.hpp
        src/client/util/LocalAddress.cpp
        src/client/util/InputCapture.hpp
        src/client/util/InputCapture.cpp
        src/ecs/systems/HealthPackSpawnerSystem.hpp
        src/ecs/systems/HealthPackSpawnerSystem.cpp
        src/ecs/components/HealthPackSpawner.hpp
        src/network/MatchConfig.hpp
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

# Copy new-renderer SPIR-V shaders next to the binary (shaders-new/ subdirectory).
add_custom_command(TARGET group2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SHADER_NEW_OUT_DIR}"
            "$<TARGET_FILE_DIR:group2>/shaders-new"
    COMMENT "Copying new-renderer shaders to binary directory"
    VERBATIM
)

# Use config.toml if present, otherwise fall back to the template.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.toml")
    set(CONFIG_TOML_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/config.toml")
else()
    set(CONFIG_TOML_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/config.template.toml")
    message(STATUS "config.toml not found — using config.template.toml")
endif()

# Copy config.toml next to the client binary.
add_custom_command(TARGET group2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CONFIG_TOML_SOURCE}"
            "$<TARGET_FILE_DIR:group2>/config.toml"
    COMMENT "Copying config.toml to client binary directory"
    VERBATIM
)

# Tell the renderer which shader formats were compiled.
if(SHADER_BUILD_MSL)
    target_compile_definitions(group2 PRIVATE HAVE_MSL_SHADERS)
endif()
if(SHADER_BUILD_DXIL)
    target_compile_definitions(group2 PRIVATE HAVE_DXIL_SHADERS)
endif()

# SDL3 GPU uses [0, 1] depth range on all backends (Vulkan + Metal).
# Without this define, glm::perspective maps depth to [-1, 1] (OpenGL convention)
# and fragments in the near half of the frustum are clipped on Metal.
target_compile_definitions(group2 PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)

# ---- Client link libraries ----
target_link_libraries(group2 PRIVATE
    SDL3::SDL3-static
    glm::glm
    SDL3_net::SDL3_net
    EnTT::EnTT
    vhacd
    imgui_lib
    assimp
    stb_image
    lunasvg::lunasvg
    minimp3
    Opus::opus
    ozz_animation_offline
    ozz_animation
    ozz_base
    tomlplusplus::tomlplusplus
)

# ---- Client precompiled header ----
# Heavy headers used across most client sources — compiled once, reused everywhere.
target_precompile_headers(group2 PRIVATE
    <string>
    <vector>
    <array>
    <memory>
    <cstdint>
    <unordered_map>
    <functional>
    <algorithm>
    <cmath>
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <glm/gtc/quaternion.hpp>
    <entt/entt.hpp>
    <SDL3/SDL.h>
)
# PCH_NO_TIMESTAMP_TARGETS — see note near top of file.
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(group2 PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()

# Copy assets (models, textures, …) next to the binary so the renderer can
# find them via SDL_GetBasePath().  Tolerate missing dir (e.g. CI without LFS).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET group2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:group2>/assets"
        COMMENT "Copying assets to binary directory"
        VERBATIM
    )
else()
    message(STATUS "assets/ directory not found — skipping asset copy (install Git LFS and run 'git lfs pull')")
endif()

# ===========================================================================
# Server executable
# ===========================================================================
add_executable(server
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/main/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/game/ServerGame.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/game/ServerGame.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/network/Server.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/network/Server.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DeveloperConfig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DeveloperConfig.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/network/DiscoveryServer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/network/DiscoveryServer.hpp"
    # PR-1 (server-perf): scoped profiler + 1 Hz aggregator.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/perf/Profiler.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/perf/Profiler.cpp"
    # PR-3 (server-perf): parallel-STL fan-out helper.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/perf/Parallel.hpp"
    # PR-18b (netsync): server-side shot-resolution log.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/perf/ShotLog.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/perf/ShotLog.cpp"
        src/ecs/systems/MovementSystem.hpp
        src/ecs/systems/CollisionSystem.hpp
        src/ecs/components/Position.hpp
        src/ecs/components/Velocity.hpp
        src/ecs/components/CollisionShape.hpp
        src/ecs/components/PlayerStateEnums.hpp
        src/ecs/components/PlayerVisState.hpp
        src/ecs/components/PlayerSimState.hpp
        src/ecs/components/InputSnapshot.hpp
        src/ecs/components/PreviousPosition.hpp
        src/ecs/components/InterpolationBuffer.hpp
        src/ecs/components/LocalPlayer.hpp
        src/ecs/physics/PhysicsConstants.hpp
        src/ecs/physics/Movement.hpp
        src/ecs/physics/Movement.cpp
        src/ecs/physics/SweptCollision.hpp
        src/ecs/physics/SweptCollision.cpp
        src/ecs/physics/MapLoader.hpp
        src/ecs/physics/MapLoader.cpp
        src/ecs/physics/TriMeshCollision.hpp
        src/ecs/physics/TriMeshCollision.cpp
        src/ecs/physics/DebugCollisionDraw.hpp
        src/ecs/physics/DebugCollisionDraw.cpp
        src/ecs/physics/PhaseDiagnostic.hpp
        src/ecs/physics/PhaseDiagnostic.cpp
        src/ecs/physics/SurfaceType.hpp
        src/ecs/physics/CollisionEvents.hpp
        src/ecs/physics/CollisionEvents.cpp
        src/ecs/physics/Forces.hpp
        src/ecs/physics/Forces.cpp
        src/ecs/physics/Inertia.hpp
        src/ecs/physics/BroadphaseTree.hpp
        src/ecs/physics/BroadphaseTree.cpp
        src/ecs/physics/ContactManifold.hpp
        src/ecs/physics/ContactCache.hpp
        src/ecs/physics/ContactCache.cpp
        src/ecs/physics/Solver.hpp
        src/ecs/physics/Solver.cpp
        src/ecs/physics/Joints.hpp
        src/ecs/physics/Joints.cpp
        src/ecs/physics/RagdollPbd.hpp
        src/ecs/physics/RagdollPbd.cpp
        src/ecs/physics/Sleep.hpp
        src/ecs/physics/Sleep.cpp
        src/ecs/physics/CookedMeshFormat.hpp
        src/ecs/physics/CookedMeshFormat.cpp
        src/ecs/physics/DeterminismHash.hpp
        src/ecs/physics/DeterminismHash.cpp
        src/ecs/physics/SimdAabb.hpp
        src/ecs/components/RigidBody.hpp
        src/ecs/components/Orientation.hpp
        src/ecs/components/Ragdoll.hpp
        src/ecs/components/TriggerVolume.hpp
        src/ecs/systems/TriggerSystem.hpp
        src/ecs/systems/TriggerSystem.cpp
        src/ecs/systems/RagdollSystem.hpp
        src/ecs/systems/RagdollSystem.cpp
        src/ecs/systems/DynamicsSystem.hpp
        src/ecs/systems/DynamicsSystem.cpp
        src/ecs/physics/VHACDImpl.cpp
        src/ecs/physics/WallDetection.hpp
        src/ecs/physics/WallDetection.cpp
        src/ecs/physics/TitanfallConstants.hpp
        src/ecs/systems/MovementSystem.hpp
        src/ecs/systems/MovementSystem.cpp
        src/ecs/systems/KinematicCharacterController.hpp
        src/ecs/systems/KinematicCharacterController.cpp
        src/ecs/systems/CollisionSystem.hpp
        src/ecs/systems/CollisionSystem.cpp
        src/server/systems/EventQueue.hpp
        src/server/systems/EventQueue.cpp
        src/server/systems/InputReceiveSystem.hpp
        src/network/MessageStream.hpp
        src/network/MessageStream.cpp
        src/network/ChatProtocol.hpp
        src/network/ChatProtocol.cpp
        src/network/NetworkConfig.hpp
        src/network/NetworkConfig.cpp
        src/network/VoiceProtocol.hpp
        src/network/VoiceProtocol.cpp
        src/network/RelayToken.hpp
        src/network/crypto/HmacSha256.hpp
        src/network/crypto/HmacSha256.cpp
        src/network/discovery/GlobalDiscoveryProtocol.hpp
        src/network/discovery/GlobalDiscoveryProtocol.cpp
        src/network/OutboundQueue.hpp
        src/network/OutboundQueue.cpp
        src/network/transport/FragmentReassembler.hpp
        src/network/transport/PacketHeader.hpp
        src/network/transport/UdpEndpoint.hpp
        src/network/transport/UdpEndpoint.cpp
        src/network/transport/UdpSessionTransport.hpp
        src/network/transport/UdpSessionTransport.cpp
        src/network/RegistryArchive.hpp
        src/network/RegistrySerialization.hpp
        src/network/RegistrySerialization.cpp
        src/ecs/components/ClientId.hpp
        src/ecs/systems/WeaponSystem.hpp
        src/ecs/systems/WeaponSystem.hpp
        src/ecs/systems/WeaponSystem.cpp
        src/ecs/systems/WeaponSystem.cpp
        src/ecs/components/WeaponConfig.hpp
        src/ecs/components/WeaponConfig.hpp
        src/ecs/components/ViewmodelConfig.hpp
        src/ecs/components/GripPose.hpp
        src/ecs/components/GripPose.cpp
        src/ecs/components/Health.hpp
        src/ecs/systems/WeaponSystem.cpp
        src/network/ShotEvent.hpp
        src/ecs/components/Explosion.hpp
        src/ecs/systems/ExplosionSystem.hpp
        src/ecs/systems/ExplosionSystem.cpp
        src/ecs/components/FireField.hpp
        src/ecs/systems/FireSystem.hpp
        src/ecs/systems/FireSystem.cpp
        src/ecs/systems/PlayerStatusSystem.hpp
        src/ecs/systems/PlayerStatusSystem.cpp
        src/ecs/components/Player.hpp
        src/network/MatchStatus.hpp
        src/server/systems/MatchController.hpp
        src/server/systems/MatchController.cpp
        src/ecs/systems/MatchSystem.hpp
        src/ecs/systems/MatchSystem.cpp
        src/network/NetKillEvent.hpp
        src/ecs/components/RespawnTimer.hpp
        src/ecs/components/DeathInfo.hpp
        src/ecs/components/WeaponSpawner.hpp
        src/ecs/components/JumpPad.hpp
        src/ecs/components/Killzone.hpp
        src/ecs/systems/WeaponSpawnerSystem.hpp
        src/ecs/systems/WeaponSpawnerSystem.cpp
        src/ecs/systems/JumpPadSystem.hpp
        src/ecs/systems/JumpPadSystem.cpp
        src/ecs/systems/KillzoneSystem.hpp
        src/ecs/systems/KillzoneSystem.cpp
        src/ecs/systems/DroppedWeaponSystem.hpp
        src/ecs/systems/DroppedWeaponSystem.cpp
        src/ecs/components/Hitbox.hpp
        src/ecs/components/HitboxHistory.hpp
        src/ecs/systems/HitboxSystem.hpp
        src/ecs/systems/HitboxSystem.cpp
        src/ecs/systems/LagCompensation.hpp
        src/ecs/components/LagCompTarget.hpp
        src/server/systems/HitboxHistorySystem.hpp
        src/server/systems/HitboxHistorySystem.cpp
        src/client/animation/CharacterRig.hpp
        src/client/animation/CharacterRig.cpp
        src/client/animation/AnimationLocomotion.hpp
        src/client/animation/AnimationLocomotion.cpp
        src/client/animation/CharacterAnimator.hpp
        src/client/animation/CharacterAnimator.cpp
        src/client/animation/AnimationLibrary.hpp
        src/client/animation/AnimationLibrary.cpp
        src/client/animation/FbxImportUtils.hpp
        src/client/animation/FbxImportUtils.cpp
        src/client/animation/SkinningBackend.hpp
        src/client/animation/SkinningBackend.cpp
        src/client/animation/SkinVertex.hpp
        src/ecs/components/AbilityState.hpp
        src/ecs/abilities/Ability.hpp
        src/ecs/abilities/AbilityTuning.hpp
        src/ecs/abilities/GrappleAbility.cpp
        src/ecs/abilities/GrappleAbility.hpp
        src/ecs/abilities/AbilityRegistry.cpp
        src/ecs/abilities/AbilityRegistry.hpp
        src/ecs/systems/AbilitySystem.cpp
        src/ecs/systems/AbilitySystem.hpp
        src/ecs/abilities/DashAbility.cpp
        src/ecs/abilities/DashAbility.hpp
        src/ecs/abilities/GravityAbility.cpp
        src/ecs/abilities/GravityAbility.hpp
        src/ecs/abilities/RecallAbility.cpp
        src/ecs/abilities/RecallAbility.hpp
        src/ecs/abilities/WallhackAbility.cpp
        src/ecs/abilities/WallhackAbility.hpp
        src/server/lobby/LobbyManager.hpp
        src/server/lobby/LobbyManager.cpp
        src/ecs/components/PowerupSpawner.hpp
        src/ecs/components/PowerupState.hpp
        src/ecs/systems/PowerupSpawnerSystem.cpp
        src/ecs/systems/PowerupSpawnerSystem.hpp
        src/ecs/systems/PowerupSystem.cpp
        src/ecs/systems/PowerupSystem.hpp
        src/ecs/systems/HealthPackSpawnerSystem.hpp
        src/ecs/systems/HealthPackSpawnerSystem.cpp
        src/ecs/components/HealthPackSpawner.hpp
)

# src/        — for shared headers (ecs/registry/Registry.hpp, etc.)
# src/server/ — for server-local headers (game/ServerGame.hpp, network/Server.hpp, etc.)
# src/client/ — needed for shared animation headers (CharacterRig, AnimationLibrary, etc.)
target_include_directories(server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client"
)

# ---- Server link libraries ----
#
# PR-3 (server-perf): TBB is required by libstdc++'s parallel STL
# (<execution> + std::execution::par_unseq). On Linux it's installed
# system-wide via setup-linux.sh / setup-archlinux.sh; we use
# `find_package` so missing TBB produces a clear error rather than a
# cryptic linker symbol miss. macOS / Windows builds with libc++ /
# MSVC's stdlib don't need this — their parallel STL ships in stdlib.
find_package(TBB QUIET)

target_link_libraries(server PRIVATE
    SDL3::SDL3-static
    SDL3_net::SDL3_net
    EnTT::EnTT
    glm::glm
    assimp
    vhacd
    tomlplusplus::tomlplusplus
    ozz_animation_offline
    ozz_animation
    ozz_base
    argparse
)
if(TBB_FOUND)
    target_link_libraries(server PRIVATE TBB::tbb)
    target_compile_definitions(server PRIVATE GROUP2_HAVE_TBB=1)
    message(STATUS "Server: TBB found; std::execution::par_unseq enabled.")
else()
    message(STATUS "Server: TBB not found; std::execution policies will be sequential.")
endif()

# ---- Server precompiled header ----
target_precompile_headers(server PRIVATE
    <string>
    <vector>
    <array>
    <memory>
    <cstdint>
    <unordered_map>
    <functional>
    <algorithm>
    <glm/glm.hpp>
    <entt/entt.hpp>
    <SDL3/SDL.h>
)
# PCH_NO_TIMESTAMP_TARGETS — see note near top of file.
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(server PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()

# Copy config.toml next to the server binary when it changes.
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/config_server.stamp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CONFIG_TOML_SOURCE}"
            "$<TARGET_FILE_DIR:server>/config.toml"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/config_server.stamp"
    DEPENDS "${CONFIG_TOML_SOURCE}"
    COMMENT "Copying config.toml to server binary directory"
    VERBATIM
)
add_custom_target(copy_server_config DEPENDS "${CMAKE_BINARY_DIR}/config_server.stamp")
add_dependencies(server copy_server_config)

# Copy map assets next to the server binary so MapLoader can find them.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps")
    add_custom_command(TARGET server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:server>/assets/maps"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps"
                "$<TARGET_FILE_DIR:server>/assets/maps"
        COMMENT "Copying map assets to server binary directory"
        VERBATIM
    )
endif()

# Copy animation assets next to the server binary for server-side hitbox animation.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/animations")
    add_custom_command(TARGET server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:server>/assets/animations"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/animations"
                "$<TARGET_FILE_DIR:server>/assets/animations"
        COMMENT "Copying animation assets to server binary directory"
        VERBATIM
    )
endif()

# ===========================================================================
# Global directory executable
# ===========================================================================
add_executable(directory_server
    "${CMAKE_CURRENT_SOURCE_DIR}/src/directory/main/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/directory/network/GlobalDirectoryServer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/directory/network/GlobalDirectoryServer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/MessageStream.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/MessageStream.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/NetworkConfig.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/NetworkConfig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/RelayToken.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/crypto/HmacSha256.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/crypto/HmacSha256.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/discovery/GlobalDiscoveryProtocol.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/discovery/GlobalDiscoveryProtocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/PacketHeader.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpEndpoint.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpEndpoint.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpSessionTransport.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpSessionTransport.cpp"
)

target_include_directories(directory_server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/directory"
)

target_link_libraries(directory_server PRIVATE
    SDL3::SDL3-static
    SDL3_net::SDL3_net
    tomlplusplus::tomlplusplus
)

if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(directory_server PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/config_directory_server.stamp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CONFIG_TOML_SOURCE}"
            "$<TARGET_FILE_DIR:directory_server>/config.toml"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/config_directory_server.stamp"
    DEPENDS "${CONFIG_TOML_SOURCE}"
    COMMENT "Copying config.toml to directory_server binary directory"
    VERBATIM
)
add_custom_target(copy_directory_server_config DEPENDS "${CMAKE_BINARY_DIR}/config_directory_server.stamp")
add_dependencies(directory_server copy_directory_server_config)

# ===========================================================================
# Clientbot executable — headless behavior-accurate bot for hit-reg /
# lag-comp testing AND server load-testing.
#
# PR-23 (netsync): the bot now extends the real client rather than just
# pushing packets at the server.  It loads the same map collision data
# (`physics::loadMapCollision` + `setActiveWorld`), runs the same
# `runPrediction` (= `runMovement` + `runCollision`) every physics tick
# on its local player, and reconciles via `runReconciliation` on each
# snapshot apply — identical code paths to `client/game/Game.cpp`.
#
# Excluded vs the real client: rendering (no window, no GPU, no shaders),
# audio (no SDL_audio), animation rigs (no ozz; bots don't drive bones,
# the server still computes hitboxes server-side via the server target).
# Added vs the real client: AI movement input generation, telemetry CSV
# capture (PR-18 obs log, PR-21 shot intent log, PR-22 client raycast).
#
# Without this the bot's `Position` only moves when a server snapshot
# applied → ray origin lagged the server's view by RTT/2 + interp_delay,
# which polluted the PR-22 hit-reg-vs-RTT measurements with self-position
# drift that doesn't exist in the real client.
# ===========================================================================
add_executable(clientbot
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clientbot/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clientbot/Bot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clientbot/Bot.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/network/Client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/network/Client.hpp"
    # PR-11 (server-perf): bot links Client.cpp so it carries the same
    # interpolation-buffer code path; the bot never reads the buffer
    # itself, but the helpers must be findable at link time.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/network/EntityInterpolation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/network/EntityInterpolation.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/MessageStream.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/MessageStream.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/ChatProtocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/ChatProtocol.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/NetworkConfig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/NetworkConfig.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/VoiceProtocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/VoiceProtocol.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/RelayToken.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/crypto/HmacSha256.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/crypto/HmacSha256.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/OutboundQueue.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/OutboundQueue.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/PacketHeader.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpEndpoint.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpEndpoint.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpSessionTransport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpSessionTransport.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/RegistrySerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/RegistrySerialization.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/RegistryArchive.hpp"
    # PR-23: physics + map-load + movement/collision systems.  These
    # match the real client exactly so prediction is byte-for-byte the
    # same.  Server's prediction-parity assumption (same world, same
    # constants, same dt) carries over verbatim.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Movement.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Movement.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/SweptCollision.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/SweptCollision.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/MapLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/MapLoader.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/TriMeshCollision.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/TriMeshCollision.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/DebugCollisionDraw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/DebugCollisionDraw.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/PhaseDiagnostic.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/PhaseDiagnostic.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/SurfaceType.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/CollisionEvents.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/CollisionEvents.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Forces.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Forces.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Inertia.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/BroadphaseTree.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/BroadphaseTree.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/ContactManifold.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/ContactCache.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/ContactCache.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Solver.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Solver.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Joints.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Joints.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/RagdollPbd.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/RagdollPbd.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Sleep.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Sleep.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/CookedMeshFormat.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/CookedMeshFormat.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/components/Ragdoll.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/RagdollSystem.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/RagdollSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/DynamicsSystem.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/DynamicsSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/DeterminismHash.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/DeterminismHash.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/SimdAabb.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/components/RigidBody.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/components/Orientation.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/components/TriggerVolume.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/TriggerSystem.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/TriggerSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/VHACDImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/WallDetection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/WallDetection.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/TitanfallConstants.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/MovementSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/MovementSystem.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/KinematicCharacterController.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/KinematicCharacterController.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/CollisionSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/CollisionSystem.hpp"
    # `runCollision` calls `queueExplosion` for projectile impacts even
    # for the local player path; on the bot the explosion queue is
    # drained by nothing (no rendering / damage source), but the symbol
    # must link.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/ExplosionSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/ExplosionSystem.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/FireSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/FireSystem.hpp"
    # `ExplosionSystem` calls `applyDamage` (player status), which
    # may call `processKill`.  None of these run their server-only
    # side effects on the bot (no kill broadcast, no respawn timer
    # broadcast); they're linked just so the predict / collide
    # path resolves.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/PlayerStatusSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/PlayerStatusSystem.hpp"
    # PlayerStatusSystem's death-drop now delegates to spawnDroppedWeapon.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/DroppedWeaponSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/DroppedWeaponSystem.hpp"
        src/ecs/components/AbilityState.hpp
        src/ecs/abilities/Ability.hpp
        src/ecs/abilities/AbilityTuning.hpp
        src/ecs/abilities/GrappleAbility.cpp
        src/ecs/abilities/GrappleAbility.hpp
        src/ecs/abilities/AbilityRegistry.cpp
        src/ecs/abilities/AbilityRegistry.hpp
        src/ecs/systems/AbilitySystem.cpp
        src/ecs/systems/AbilitySystem.hpp
        src/ecs/abilities/DashAbility.cpp
        src/ecs/abilities/DashAbility.hpp
        src/ecs/abilities/GravityAbility.cpp
        src/ecs/abilities/GravityAbility.hpp
        src/ecs/abilities/RecallAbility.cpp
        src/ecs/abilities/RecallAbility.hpp
        src/ecs/abilities/WallhackAbility.cpp
        src/ecs/abilities/WallhackAbility.hpp
        src/ecs/components/PowerupSpawner.hpp
        src/ecs/components/PowerupState.hpp
        src/ecs/systems/PowerupSpawnerSystem.cpp
        src/ecs/systems/PowerupSpawnerSystem.hpp
        src/ecs/systems/PowerupSystem.cpp
        src/ecs/systems/PowerupSystem.hpp
)

# Headers under src/ are referenced via "network/...", "ecs/components/...".
target_include_directories(clientbot PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client"
)

# Bot links the same map/prop collision loader dependencies as the client.
# The `vhacd` target is empty unless GROUP2_ENABLE_VHACD is explicitly enabled;
# production map collision stays on authored trimeshes.
target_link_libraries(clientbot PRIVATE
    SDL3::SDL3-static
    SDL3_net::SDL3_net
    EnTT::EnTT
    glm::glm
    tomlplusplus::tomlplusplus
    assimp
    vhacd
)

# Copy config.toml next to clientbot so it picks up the same server address
# the real client uses by default.
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/config_clientbot.stamp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CONFIG_TOML_SOURCE}"
            "$<TARGET_FILE_DIR:clientbot>/config.toml"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/config_clientbot.stamp"
    DEPENDS "${CONFIG_TOML_SOURCE}"
    COMMENT "Copying config.toml to clientbot binary directory"
    VERBATIM
)
add_custom_target(copy_clientbot_config DEPENDS "${CMAKE_BINARY_DIR}/config_clientbot.stamp")
add_dependencies(clientbot copy_clientbot_config)

enable_testing()

add_executable(physics_trimesh_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/physics_trimesh_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/SweptCollision.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/TriMeshCollision.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/WallDetection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Movement.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Forces.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/MovementSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/KinematicCharacterController.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/DebugCollisionDraw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/PhaseDiagnostic.cpp"
)
target_include_directories(physics_trimesh_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(physics_trimesh_tests PRIVATE
    EnTT::EnTT
    glm::glm
)
add_test(NAME physics_trimesh_tests COMMAND physics_trimesh_tests)

add_executable(ragdoll_system_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ragdoll_system_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/systems/RagdollSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Joints.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/RagdollPbd.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ecs/physics/Forces.cpp"
)
target_include_directories(ragdoll_system_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(ragdoll_system_tests PRIVATE
    EnTT::EnTT
    glm::glm
)
add_test(NAME ragdoll_system_tests COMMAND ragdoll_system_tests)

add_executable(input_bindings_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/input_bindings_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/config/InputBindings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/config/InputBindings.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/config/UserSettings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/config/UserSettings.hpp"
)
target_include_directories(input_bindings_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client"
)
target_link_libraries(input_bindings_tests PRIVATE
    SDL3::SDL3-static
    tomlplusplus::tomlplusplus
)
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(input_bindings_tests PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()
set_compiler_warnings(input_bindings_tests)
enable_sanitizers(input_bindings_tests)
add_test(NAME input_bindings_tests COMMAND input_bindings_tests)

add_executable(udp_session_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/udp_session_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/PacketHeader.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/FragmentReassembler.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/RelayToken.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/crypto/HmacSha256.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/crypto/HmacSha256.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpEndpoint.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpEndpoint.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpSessionTransport.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/UdpSessionTransport.cpp"
)
target_include_directories(udp_session_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(udp_session_tests PRIVATE
    SDL3::SDL3-static
    SDL3_net::SDL3_net
)
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(udp_session_tests PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()
set_compiler_warnings(udp_session_tests)
enable_sanitizers(udp_session_tests)
add_test(NAME udp_session_tests COMMAND udp_session_tests)

add_executable(chat_voice_protocol_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/chat_voice_protocol_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/ChatProtocol.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/ChatProtocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/VoiceProtocol.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/VoiceProtocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/transport/PacketHeader.hpp"
)
target_include_directories(chat_voice_protocol_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(chat_voice_protocol_tests PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()
set_compiler_warnings(chat_voice_protocol_tests)
enable_sanitizers(chat_voice_protocol_tests)
add_test(NAME chat_voice_protocol_tests COMMAND chat_voice_protocol_tests)

add_executable(audio_math_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_math_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/AudioMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/AudioMath.hpp"
)
target_include_directories(audio_math_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(audio_math_tests PRIVATE
    glm::glm
)
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(audio_math_tests PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()
set_compiler_warnings(audio_math_tests)
enable_sanitizers(audio_math_tests)
add_test(NAME audio_math_tests COMMAND audio_math_tests)

add_executable(animation_locomotion_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/animation_locomotion_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/AnimationLocomotion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/animation/AnimationLocomotion.hpp"
)
target_include_directories(animation_locomotion_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(animation_locomotion_tests PRIVATE
    glm::glm
)
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(animation_locomotion_tests PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()
set_compiler_warnings(animation_locomotion_tests)
enable_sanitizers(animation_locomotion_tests)
add_test(NAME animation_locomotion_tests COMMAND animation_locomotion_tests)

add_executable(lag_comp_math_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/lag_comp_math_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/game/LagCompMath.hpp"
)
target_include_directories(lag_comp_math_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(lag_comp_math_tests PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()
set_compiler_warnings(lag_comp_math_tests)
enable_sanitizers(lag_comp_math_tests)
add_test(NAME lag_comp_math_tests COMMAND lag_comp_math_tests)

add_executable(voice_jitter_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/voice_jitter_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/voice/VoiceJitterBuffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/voice/VoiceJitterBuffer.hpp"
)
target_include_directories(voice_jitter_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(voice_jitter_tests PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()
set_compiler_warnings(voice_jitter_tests)
enable_sanitizers(voice_jitter_tests)
add_test(NAME voice_jitter_tests COMMAND voice_jitter_tests)

add_executable(audio_runtime_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_runtime_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/AudioRuntime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/AudioRuntime.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/sfx/SfxTypes.hpp"
)
target_include_directories(audio_runtime_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(audio_runtime_tests PRIVATE
    SDL3::SDL3-static
    glm::glm
    tomlplusplus::tomlplusplus
)
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(audio_runtime_tests PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()
set_compiler_warnings(audio_runtime_tests)
enable_sanitizers(audio_runtime_tests)
add_test(NAME audio_runtime_tests COMMAND audio_runtime_tests)

add_executable(tracer_effect_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tracer_effect_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/particles/effects/TracerEffect.cpp"
)
target_include_directories(tracer_effect_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client"
)
target_link_libraries(tracer_effect_tests PRIVATE
    EnTT::EnTT
    glm::glm
)
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(tracer_effect_tests PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()
set_compiler_warnings(tracer_effect_tests)
enable_sanitizers(tracer_effect_tests)
add_test(NAME tracer_effect_tests COMMAND tracer_effect_tests)

add_executable(hud_svg_raster_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/hud_svg_raster_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudSvgRaster.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudSvgRaster.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudTypes.hpp"
)
target_include_directories(hud_svg_raster_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client"
)
target_link_libraries(hud_svg_raster_tests PRIVATE
    SDL3::SDL3-static
    glm::glm
    lunasvg::lunasvg
)
target_compile_definitions(hud_svg_raster_tests PRIVATE
    GROUP2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
if(PCH_NO_TIMESTAMP_FLAG)
    target_compile_options(hud_svg_raster_tests PRIVATE ${PCH_NO_TIMESTAMP_FLAG})
endif()
set_compiler_warnings(hud_svg_raster_tests)
enable_sanitizers(hud_svg_raster_tests)
add_test(NAME hud_svg_raster_tests COMMAND hud_svg_raster_tests)

# ---- Compiler warnings ----
set_compiler_warnings(group2)
set_compiler_warnings(server)
set_compiler_warnings(directory_server)
set_compiler_warnings(clientbot)
set_compiler_warnings(physics_trimesh_tests)
set_compiler_warnings(ragdoll_system_tests)

# ---- Sanitizers ----
enable_sanitizers(group2)
enable_sanitizers(server)
enable_sanitizers(directory_server)
enable_sanitizers(clientbot)
enable_sanitizers(physics_trimesh_tests)
enable_sanitizers(ragdoll_system_tests)

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
            COMMENT "Copying MSVC ASan runtime DLL to group2 output directory"
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

# ---- Symlink compile_commands.json to project root for clang-tidy / LSP ----
# Done at configure time (not build time) so parallel builds of different
# presets don't race to overwrite the same symlink.  The link points to
# whichever preset was last configured — re-run cmake --preset <name> to switch.
if(NOT WIN32 AND CMAKE_EXPORT_COMPILE_COMMANDS)
    file(CREATE_LINK
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
        SYMBOLIC
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
