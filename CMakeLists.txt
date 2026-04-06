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
option(USE_OPENGL   "Use OpenGL 4.1 core backend instead of SDL3 GPU"  OFF)

# Embed compiled SPIR-V into the binary (SDL3 GPU path only).
# Defaults to ON for Release/RelWithDebInfo so no shaders/ folder is
# needed at runtime.  Always OFF for the OpenGL backend (no SPIR-V).
if(NOT USE_OPENGL AND CMAKE_BUILD_TYPE MATCHES "^Rel")
    set(_bundle_shaders_default ON)
else()
    set(_bundle_shaders_default OFF)
endif()
option(GROUP2_BUNDLE_SHADERS
    "Embed SPIR-V shaders into the executable (SDL3 GPU path, Release builds)"
    ${_bundle_shaders_default})

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

# EnTT — fast header-only ECS library (always enabled)
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.14.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(EnTT)

# glad — OpenGL 4.1 core function loader, generated via Python3 (optional)
# Must be declared before imgui so the glad target exists when imgui links it.
if(USE_OPENGL)
    FetchContent_Declare(
        glad
        GIT_REPOSITORY https://github.com/Dav1dde/glad.git
        GIT_TAG        v0.1.36
        GIT_SHALLOW    TRUE
    )
    set(GLAD_PROFILE    "core"   CACHE STRING "" FORCE)
    set(GLAD_API        "gl=4.1" CACHE STRING "" FORCE)
    set(GLAD_GENERATOR  "c"      CACHE STRING "" FORCE)
    set(GLAD_EXTENSIONS ""       CACHE STRING "" FORCE)
    FetchContent_MakeAvailable(glad)
endif()

# Dear ImGui — immediate-mode GUI (dev overlay; SDL3 GPU + OpenGL backends)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    # Shared SDL3 platform backend used by both the SDL3 GPU and OpenGL render paths.
    # imgui_demo.cpp is intentionally excluded (debug demo window not needed in shipping builds).
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
)
if(USE_OPENGL)
    list(APPEND IMGUI_SOURCES
        "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
else()
    list(APPEND IMGUI_SOURCES
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp")
endif()

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(imgui PRIVATE SDL3::SDL3-static)
if(USE_OPENGL)
    target_link_libraries(imgui PRIVATE glad)
endif()
# Suppress warnings from imgui itself (GNU/Clang/AppleClang on Linux/macOS, MSVC on Windows)
target_compile_options(imgui PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w>
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
)

# ---- Shaders (SDL3 GPU path only) ----
if(NOT USE_OPENGL)
    # ---- GLSL → SPIR-V compiler ----
    # Prefers glslc (shaderc); falls back to glslangValidator.
    #   Arch:    sudo pacman -S shaderc            (glslc)
    #   Ubuntu:  sudo apt-get install glslang-tools (glslangValidator)
    #   macOS:   brew install glslang              (glslangValidator)
    #   Windows: install Vulkan SDK                (glslc)
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
    elseif(GLSLANG_VALIDATOR)
        message(STATUS "Shader compiler: glslangValidator (${GLSLANG_VALIDATOR})")
        set(SHADER_COMPILE_TOOL  "${GLSLANG_VALIDATOR}")
        set(SHADER_COMPILE_FLAGS "-V")      # -V = output SPIR-V binary
    else()
        message(FATAL_ERROR
            "No GLSL→SPIR-V compiler found.\n"
            "  Arch:    sudo pacman -S shaderc\n"
            "  Ubuntu:  sudo apt-get install glslang-tools\n"
            "  macOS:   brew install glslang\n"
            "  Windows: install the Vulkan SDK (https://vulkan.lunarg.com/)")
    endif()

    # ---- SPIR-V → MSL transpiler (macOS Metal backend) ----
    # Required on macOS; optional elsewhere (enables MSL shaders on all platforms).
    #   macOS:   brew install spirv-cross
    #   Ubuntu:  sudo apt-get install spirv-cross
    #   Windows: bundled with the Vulkan SDK
    find_program(SPIRV_CROSS spirv-cross
        HINTS ENV VULKAN_SDK
        PATHS /opt/homebrew/bin /usr/local/bin
        PATH_SUFFIXES bin)
    if(SPIRV_CROSS)
        message(STATUS "MSL transpiler: spirv-cross (${SPIRV_CROSS})")
    elseif(APPLE)
        message(FATAL_ERROR
            "spirv-cross not found — required on macOS for the SDL3 GPU Metal backend.\n"
            "  brew install spirv-cross\n"
            "  See scripts/setup-macos.sh")
    else()
        message(STATUS "spirv-cross not found — MSL shaders will not be built (non-macOS)")
    endif()

    set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

    set(SHADER_SOURCES
        triangle.vert
        triangle.frag
        ultralight/fill.vert
        ultralight/fill.frag
        ultralight/fill_path.vert
        ultralight/fill_path.frag
        ultralight/composite.vert
        ultralight/composite.frag
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
            COMMENT "Compiling shader ${SHADER_SRC} to SPIR-V"
            VERBATIM
        )
        list(APPEND SHADER_OUTPUTS "${SPV_OUT}")

        # SPIR-V → MSL (for SDL3 GPU Metal backend on macOS)
        if(SPIRV_CROSS)
            set(MSL_OUT "${SHADER_OUT_DIR}/${SHADER_SRC}.msl")
            add_custom_command(
                OUTPUT  "${MSL_OUT}"
                COMMAND "${SPIRV_CROSS}" --msl "${SPV_OUT}" --output "${MSL_OUT}"
                DEPENDS "${SPV_OUT}"
                COMMENT "Transpiling shader ${SHADER_SRC} to MSL"
                VERBATIM
            )
            list(APPEND SHADER_OUTPUTS "${MSL_OUT}")
        endif()
    endforeach()

    add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})
endif()

# ---- Sources ----
set(SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/imgui_layer.cpp"
)
if(USE_OPENGL)
    list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/OpenGLRenderer.cpp")
else()
    list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/SDLGPURenderer.cpp")
endif()

# ===========================================================================
# Executable
# ===========================================================================
add_executable(group2 ${SOURCES})

target_include_directories(group2 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
)

# SDL3 GPU path: compile shaders before linking the binary.
if(NOT USE_OPENGL)
    add_dependencies(group2 shaders)
endif()

# ---- Link libraries ----
target_link_libraries(group2 PRIVATE
    SDL3::SDL3-static
    glm::glm
    EnTT::EnTT
    imgui
)

if(USE_OPENGL)
    target_link_libraries(group2 PRIVATE glad)
    target_compile_definitions(group2 PRIVATE USE_OPENGL)
else()
    # Tell the renderer which shader formats were compiled so it can advertise
    # the right formats to SDL_CreateGPUDevice at runtime.
    if(SPIRV_CROSS)
        target_compile_definitions(group2 PRIVATE HAVE_MSL_SHADERS)
    endif()
    # ---- Shader bundling (SDL3 GPU release builds) ----
    if(GROUP2_BUNDLE_SHADERS)
        # Write the list of compiled shader paths to a file; the embed script reads
        # it at build time (avoids CMake semicolon-escaping in -D arguments).
        string(REPLACE ";" "\n" _shader_list_content "${SHADER_OUTPUTS}")
        file(WRITE "${CMAKE_BINARY_DIR}/shader_list.txt" "${_shader_list_content}")

        set(_embedded_header "${CMAKE_BINARY_DIR}/embedded_shaders.hpp")

        add_custom_command(
            OUTPUT  "${_embedded_header}"
            COMMAND ${CMAKE_COMMAND}
                    "-DSPV_LIST_FILE=${CMAKE_BINARY_DIR}/shader_list.txt"
                    "-DOUTPUT_FILE=${_embedded_header}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
            DEPENDS ${SHADER_OUTPUTS}
            COMMENT "Embedding shaders (SPIR-V + MSL) into the executable"
            VERBATIM
        )
        add_custom_target(embed_shaders DEPENDS "${_embedded_header}")
        add_dependencies(group2 embed_shaders)

        # Let SDLGPURenderer.cpp find the generated header.
        target_include_directories(group2 PRIVATE "${CMAKE_BINARY_DIR}")
        target_compile_definitions(group2 PRIVATE BUNDLE_SHADERS)
        message(STATUS "Shader bundling: ON  — shaders compiled into the executable")
    else()
        # Copy compiled shaders next to the binary so SDL_GetBasePath() finds them.
        add_custom_command(TARGET group2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${SHADER_OUT_DIR}"
                    "$<TARGET_FILE_DIR:group2>/shaders"
            COMMENT "Copying shaders to binary directory"
            VERBATIM
        )
        message(STATUS "Shader bundling: OFF — shaders/ folder required next to the binary")
    endif()
endif()

# ---- Compiler warnings ----
set_compiler_warnings(group2)

# ---- Sanitizers ----
enable_sanitizers(group2)

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
