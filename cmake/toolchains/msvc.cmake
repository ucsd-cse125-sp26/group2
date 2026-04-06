# cmake/toolchains/msvc.cmake
# Auto-detect MSVC + Windows SDK so Ninja+cl works from ANY environment
# (CLion, VSCode, plain terminal) — no Developer PowerShell required.
#
# If cl.exe is already on PATH (developer shell), this is a no-op.

cmake_minimum_required(VERSION 3.25)

# ── Early exit if already in a developer environment ─────────────────
find_program(_cl_on_path cl)
if(_cl_on_path)
    set(CMAKE_C_COMPILER   "${_cl_on_path}" CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER "${_cl_on_path}" CACHE FILEPATH "" FORCE)
    return()
endif()

# ── Find Visual Studio via vswhere ───────────────────────────────────
set(_vswhere "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
if(NOT EXISTS "${_vswhere}")
    message(FATAL_ERROR
        "vswhere.exe not found — no Visual Studio installation detected.\n"
        "Run scripts/setup-windows.ps1 to install VS Build Tools.")
endif()

execute_process(
    COMMAND "${_vswhere}" "-version" "[17.0,18.0)" "-products" "*" "-requires" "Microsoft.VisualStudio.Component.VC.Tools.x86.x64" "-property" "installationPath"
    OUTPUT_VARIABLE _vs_path
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _vswhere_rc
)
if(NOT _vswhere_rc EQUAL 0 OR NOT _vs_path)
    message(FATAL_ERROR
        "No VS 2022 installation with C++ tools found.\n"
        "Run scripts/setup-windows.ps1 to install VS Build Tools.")
endif()

# ── Locate MSVC tools ────────────────────────────────────────────────
file(READ "${_vs_path}/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" _msvc_ver)
string(STRIP "${_msvc_ver}" _msvc_ver)
set(_msvc_dir "${_vs_path}/VC/Tools/MSVC/${_msvc_ver}")

if(NOT EXISTS "${_msvc_dir}/bin/Hostx64/x64/cl.exe")
    message(FATAL_ERROR "cl.exe not found at ${_msvc_dir}/bin/Hostx64/x64/cl.exe")
endif()

set(_msvc_bin "${_msvc_dir}/bin/Hostx64/x64")
set(CMAKE_C_COMPILER   "${_msvc_bin}/cl.exe"   CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_msvc_bin}/cl.exe"   CACHE FILEPATH "" FORCE)
set(CMAKE_LINKER        "${_msvc_bin}/link.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_AR            "${_msvc_bin}/lib.exe"  CACHE FILEPATH "" FORCE)

# ── Locate Windows SDK ───────────────────────────────────────────────
set(_winsdk_root "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
if(NOT EXISTS "${_winsdk_root}/Include")
    message(FATAL_ERROR "Windows 10 SDK not found at ${_winsdk_root}")
endif()

# Pick the newest SDK version
file(GLOB _sdk_versions LIST_DIRECTORIES true "${_winsdk_root}/Include/10.*")
list(SORT _sdk_versions COMPARE NATURAL ORDER DESCENDING)
list(GET _sdk_versions 0 _sdk_version_dir)
get_filename_component(_sdk_version "${_sdk_version_dir}" NAME)

set(_sdk_bin "${_winsdk_root}/bin/${_sdk_version}/x64")
set(CMAKE_RC_COMPILER "${_sdk_bin}/rc.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_MT          "${_sdk_bin}/mt.exe" CACHE FILEPATH "" FORCE)

# ── System include directories ───────────────────────────────────────
# These are passed as /external:I flags so warnings in system headers
# are suppressed, exactly like the developer environment would do.
set(_includes
    "${_msvc_dir}/include"
    "${_winsdk_root}/Include/${_sdk_version}/ucrt"
    "${_winsdk_root}/Include/${_sdk_version}/um"
    "${_winsdk_root}/Include/${_sdk_version}/shared"
    "${_winsdk_root}/Include/${_sdk_version}/winrt"
    "${_winsdk_root}/Include/${_sdk_version}/cppwinrt"
)
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${_includes} CACHE STRING "" FORCE)
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES   ${_includes} CACHE STRING "" FORCE)

# Also set INCLUDE env — some tools (rc.exe) read it directly.
list(JOIN _includes ";" _include_env)
set(ENV{INCLUDE} "${_include_env}")

# ── System library directories ───────────────────────────────────────
# Embedded into link commands via CMAKE_*_LINKER_FLAGS_INIT so they
# persist at build time (ENV{LIB} only lives during configure).
set(_msvc_lib "${_msvc_dir}/lib/x64")
set(_ucrt_lib "${_winsdk_root}/Lib/${_sdk_version}/ucrt/x64")
set(_um_lib   "${_winsdk_root}/Lib/${_sdk_version}/um/x64")

set(_libpath "/LIBPATH:\"${_msvc_lib}\" /LIBPATH:\"${_ucrt_lib}\" /LIBPATH:\"${_um_lib}\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_libpath}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_libpath}" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_libpath}" CACHE STRING "" FORCE)

# LIB env for configure-time detection
set(ENV{LIB} "${_msvc_lib};${_ucrt_lib};${_um_lib}")

# ── PATH — add tool directories for configure-time probes ────────────
set(ENV{PATH} "${_msvc_bin};${_sdk_bin};$ENV{PATH}")
