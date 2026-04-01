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

# ---- Sources ----
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(titandoom ${GAME_SOURCES})
add_dependencies(titandoom shaders)

# Copy compiled shaders into <build>/shaders/ next to the binary at post-build.
# SDL_GetBasePath() + "shaders/<name>.spv" is how the binary locates them at runtime.
add_custom_command(TARGET titandoom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:titandoom>/shaders"
    COMMENT "Copying shaders to binary directory"
    VERBATIM
)

target_include_directories(titandoom PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(titandoom PRIVATE
    SDL3::SDL3-static
    EnTT::EnTT
    glm::glm
)

if(ENABLE_SHADERCROSS)
    target_link_libraries(titandoom PRIVATE SDL_shadercross::SDL_shadercross)
    target_compile_definitions(titandoom PRIVATE ENABLE_SHADERCROSS)
endif()

# ---- Compiler warnings ----
set_compiler_warnings(titandoom)

# ---- Sanitizers ----
enable_sanitizers(titandoom)

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
