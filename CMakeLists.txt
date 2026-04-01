cmake_minimum_required(VERSION 3.25)

project(titandoom
    VERSION 0.1.0
    DESCRIPTION "3D Multiplayer FPS"
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

# ---- Options ----
option(ENABLE_ASAN        "Enable AddressSanitizer"           OFF)
option(ENABLE_UBSAN       "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN        "Enable ThreadSanitizer"            OFF)
option(ENABLE_SHADERCROSS "Build with SDL_shadercross"        OFF)

# Embed compiled SPIR-V into the executable so no shaders/ folder is needed at runtime.
# Defaults to ON for Release/RelWithDebInfo, OFF for Debug (keeps hot-reload possible).
if(CMAKE_BUILD_TYPE MATCHES "^Rel")
    set(_bundle_shaders_default ON)
else()
    set(_bundle_shaders_default OFF)
endif()
option(TITANDOOM_BUNDLE_SHADERS
    "Embed SPIR-V shaders into the executable (single-binary distribution)"
    ${_bundle_shaders_default})

# ---- Dependencies ----

# SDL3 — window, input, GPU API (Vulkan/Metal/DX12), audio
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.0
    GIT_SHALLOW    TRUE
)
# Build SDL3 as a static library so no DLL shipping is needed
set(SDL_SHARED    OFF CACHE BOOL "" FORCE)
set(SDL_STATIC    ON  CACHE BOOL "" FORCE)
# SDL3 3.2.0 has a type mismatch in the PipeWire backend vs newer PipeWire headers;
# disable it for now — PulseAudio / ALSA remain available on Linux.
set(SDL_PIPEWIRE  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# EnTT — fast, header-only ECS library
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.14.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(EnTT)

# GLM — header-only math library (vectors, matrices, quaternions)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# SDL_shadercross — HLSL → SPIR-V / MSL / DXIL (optional, see README)
if(ENABLE_SHADERCROSS)
    FetchContent_Declare(
        SDL_shadercross
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_shadercross.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL_shadercross)
endif()

# ---- Shaders ----
# Compile GLSL → SPIR-V at build time.
# Prefers glslc (shaderc); falls back to glslangValidator.
#   Arch:    sudo pacman -S shaderc        (glslc)
#   Ubuntu:  sudo apt-get install glslang-tools  (glslangValidator)
#   macOS:   brew install glslang         (glslangValidator)
#   Windows: install Vulkan SDK           (glslc)
find_program(GLSLC glslc
    HINTS ENV VULKAN_SDK
    PATHS /opt/homebrew/bin /usr/local/bin   # Homebrew macOS
    PATH_SUFFIXES bin)
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS ENV VULKAN_SDK
    PATHS /opt/homebrew/bin /usr/local/bin   # Homebrew macOS
    PATH_SUFFIXES bin)

if(GLSLC)
    message(STATUS "Shader compiler: glslc (${GLSLC})")
    set(SHADER_COMPILE_TOOL "${GLSLC}")
    set(SHADER_COMPILE_FLAGS "")         # glslc infers stage from .vert/.frag extension
elseif(GLSLANG_VALIDATOR)
    message(STATUS "Shader compiler: glslangValidator (${GLSLANG_VALIDATOR})")
    set(SHADER_COMPILE_TOOL "${GLSLANG_VALIDATOR}")
    set(SHADER_COMPILE_FLAGS "-V")       # -V = output SPIR-V binary
else()
    message(FATAL_ERROR
        "No GLSL→SPIR-V compiler found.\n"
        "  Arch:    sudo pacman -S shaderc\n"
        "  Ubuntu:  sudo apt-get install glslang-tools\n"
        "  macOS:   brew install glslang\n"
        "  Windows: install the Vulkan SDK (https://vulkan.lunarg.com/)")
endif()

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

set(SHADER_SOURCES
    triangle.vert
    triangle.frag
    scene.vert
    scene.frag
    hud.vert
    hud.frag
)

set(SHADER_OUTPUTS "")
foreach(SHADER_SRC ${SHADER_SOURCES})
    set(SPV_OUT "${SHADER_OUT_DIR}/${SHADER_SRC}.spv")
    add_custom_command(
        OUTPUT  "${SPV_OUT}"
        COMMAND ${SHADER_COMPILE_TOOL} ${SHADER_COMPILE_FLAGS}
                -o "${SPV_OUT}" "${SHADER_SRC_DIR}/${SHADER_SRC}"
        DEPENDS "${SHADER_SRC_DIR}/${SHADER_SRC}"
        COMMENT "Compiling shader ${SHADER_SRC}"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS "${SPV_OUT}")
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})

# ---- Platform socket library ----
if(WIN32)
    set(SOCKET_LIBS ws2_32)
else()
    set(SOCKET_LIBS "")
endif()

# ---- Shared game / networking sources (used by both client and server) ----
set(COMMON_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/Socket.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/NetChannel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/Physics.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/Weapons.cpp"
)

# ---- Client sources ----
set(CLIENT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/Renderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/MeshGen.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/HUD.cpp"
)

# ---- Server sources ----
set(SERVER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server_main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/GameServer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server/LagComp.cpp"
)

# ===========================================================================
# Client executable
# ===========================================================================
add_executable(titandoom ${CLIENT_SOURCES} ${COMMON_SOURCES})
add_dependencies(titandoom shaders)

if(TITANDOOM_BUNDLE_SHADERS)
    # ---- Embed SPIR-V into a generated C++ header ----
    # Write the list of compiled SPV paths to a file; the script reads it at build time
    # (avoids CMake semicolon-escaping issues when passing lists via -D to cmake -P).
    string(REPLACE ";" "\n" _spv_list_content "${SHADER_OUTPUTS}")
    file(WRITE "${CMAKE_BINARY_DIR}/spv_list.txt" "${_spv_list_content}")

    set(_embedded_header "${CMAKE_BINARY_DIR}/embedded_shaders.hpp")

    add_custom_command(
        OUTPUT  "${_embedded_header}"
        COMMAND ${CMAKE_COMMAND}
                "-DSPV_LIST_FILE=${CMAKE_BINARY_DIR}/spv_list.txt"
                "-DOUTPUT_FILE=${_embedded_header}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
        DEPENDS ${SHADER_OUTPUTS}
        COMMENT "Embedding SPIR-V shaders into the executable"
        VERBATIM
    )
    add_custom_target(embed_shaders DEPENDS "${_embedded_header}")
    add_dependencies(titandoom embed_shaders)

    # Let Renderer.cpp find the generated header via #include "embedded_shaders.hpp"
    target_include_directories(titandoom PRIVATE "${CMAKE_BINARY_DIR}")
    target_compile_definitions(titandoom PRIVATE TITANDOOM_BUNDLE_SHADERS)
    message(STATUS "Shader bundling: ON  — shaders compiled into the executable")
else()
    # Copy compiled shaders next to the binary; SDL_GetBasePath() locates them at runtime.
    add_custom_command(TARGET titandoom POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SHADER_OUT_DIR}"
                "$<TARGET_FILE_DIR:titandoom>/shaders"
        COMMENT "Copying shaders to binary directory"
        VERBATIM
    )
    message(STATUS "Shader bundling: OFF — shaders/ folder required next to the binary")
endif()

target_link_libraries(titandoom PRIVATE
    SDL3::SDL3-static
    EnTT::EnTT
    glm::glm
    ${SOCKET_LIBS}
)

if(ENABLE_SHADERCROSS)
    target_link_libraries(titandoom PRIVATE SDL_shadercross::SDL_shadercross)
    target_compile_definitions(titandoom PRIVATE ENABLE_SHADERCROSS)
endif()

# ---- Compiler warnings ----
set_compiler_warnings(titandoom)

# ---- Sanitizers ----
enable_sanitizers(titandoom)

# ===========================================================================
# Dedicated server executable
# ===========================================================================
add_executable(titandoom-server ${SERVER_SOURCES} ${COMMON_SOURCES})
add_dependencies(titandoom-server shaders)

target_include_directories(titandoom-server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(titandoom-server PRIVATE
    SDL3::SDL3-static
    EnTT::EnTT
    glm::glm
    ${SOCKET_LIBS}
)

set_compiler_warnings(titandoom-server)
enable_sanitizers(titandoom-server)

# Server always loads shaders from disk (for log msgs about SPV files, future use)
# No shaders needed at server runtime currently — just copy for completeness.
add_custom_command(TARGET titandoom-server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SHADER_OUT_DIR}"
            "$<TARGET_FILE_DIR:titandoom-server>/shaders"
    COMMENT "Copying shaders to server binary directory"
    VERBATIM
)

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
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    # Collect all project sources (exclude build/_deps)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_SOURCE_DIR}/src/*.h"
    )

    # `cmake --build . --target format`  — reformat everything in-place
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format (in-place) on all sources"
        VERBATIM
    )

    # `cmake --build . --target format-check`  — exit non-zero if anything is unformatted
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCE_FILES}
        COMMENT "Checking clang-format compliance"
        VERBATIM
    )

    # Install a git pre-commit hook that auto-formats staged .cpp/.hpp/.h files.
    # Runs once at configure time; safe to re-run (idempotent).
    if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
        set(HOOK_DIR "${CMAKE_SOURCE_DIR}/.git/hooks")
        set(HOOK_FILE "${HOOK_DIR}/pre-commit")
        file(WRITE "${HOOK_FILE}"
"#!/usr/bin/env bash
# Auto-generated by CMake — clang-format pre-commit hook.
# Formats staged C++ files and re-adds them so the commit always matches style.
set -e
STAGED=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\\.(cpp|hpp|h)$' || true)
if [ -z \"$STAGED\" ]; then exit 0; fi
echo \"[pre-commit] clang-format: $STAGED\"
echo \"$STAGED\" | xargs clang-format -i
echo \"$STAGED\" | xargs git add
")
        file(CHMOD "${HOOK_FILE}" PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE
        )
        message(STATUS "Installed git pre-commit clang-format hook → ${HOOK_FILE}")
    endif()
else()
    message(WARNING "clang-format not found — format/format-check targets unavailable")
endif()
