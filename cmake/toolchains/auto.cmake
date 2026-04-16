# cmake/toolchains/auto.cmake
# Auto-detect the right compiler for the host platform so a single set of
# CMake presets works on Linux, macOS, and Windows — no -mac/-win suffixes.
#
#   Linux   → clang / clang++ from PATH
#   macOS   → /usr/bin/clang  (Apple Clang — avoids Homebrew LLVM in PATH)
#   Windows → MSVC via cmake/toolchains/msvc.cmake

cmake_minimum_required(VERSION 3.25)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    include("${CMAKE_CURRENT_LIST_DIR}/msvc.cmake")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    # The setup-macos.sh script adds Homebrew LLVM 18 to PATH for
    # clang-format-18, which shadows the system clang.  Homebrew's clang
    # cannot find macOS SDK C headers (<stddef.h>, etc.), so we must use
    # Apple Clang via absolute path.
    set(CMAKE_C_COMPILER   /usr/bin/clang)
    set(CMAKE_CXX_COMPILER /usr/bin/clang++)
else()
    set(CMAKE_C_COMPILER   clang)
    set(CMAKE_CXX_COMPILER clang++)
endif()
