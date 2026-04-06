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

# ---- Sources ----
set(SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/renderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/renderer.h"
)

# ===========================================================================
# Executable
# ===========================================================================
add_executable(group2 ${SOURCES})

target_include_directories(group2 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# ---- Link libraries ----
target_link_libraries(group2 PRIVATE
    SDL3::SDL3-static
    glm::glm
)

if(USE_ENTT)
    target_link_libraries(group2 PRIVATE EnTT::EnTT)
    target_compile_definitions(group2 PRIVATE USE_ENTT)
endif()

# ---- Compiler warnings ----
set_compiler_warnings(group2)

# ---- Sanitizers ----
enable_sanitizers(group2)

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
