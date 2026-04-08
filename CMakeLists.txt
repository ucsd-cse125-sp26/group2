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

# ---- Options ----
option(ENABLE_ASAN  "Enable AddressSanitizer"                           OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer"                 OFF)
option(ENABLE_TSAN  "Enable ThreadSanitizer"                            OFF)
option(USE_ENTT     "Use EnTT ECS library (OFF = minimal stub)"        OFF)

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

# EnTT — fast header-only ECS library (optional)
if(USE_ENTT)
    FetchContent_Declare(
        EnTT
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG        v3.14.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(EnTT)
endif()

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

set(SHADER_SOURCES triangle.vert triangle.frag)
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
)

if(USE_ENTT)
    target_link_libraries(group2 PRIVATE EnTT::EnTT)
    target_compile_definitions(group2 PRIVATE USE_ENTT)
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
)

if(USE_ENTT)
    target_link_libraries(server PRIVATE EnTT::EnTT)
    target_compile_definitions(server PRIVATE USE_ENTT)
endif()

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
