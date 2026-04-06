# cmake/Ultralight.cmake
# Downloads the Ultralight v1.4.0 prebuilt SDK for the current platform,
# creates imported shared-library targets, and schedules post-build steps
# to copy shared libs + resources next to the binary.
#
# Targets created:
#   Ultralight::Core     — UltralightCore
#   Ultralight::Web      — WebCore
#   Ultralight::UI       — Ultralight

set(UL_VERSION "1.4.0")
set(UL_BASE_URL "https://github.com/ultralight-ux/Ultralight/releases/download/v${UL_VERSION}")

# Detect platform + architecture
if(WIN32)
    set(UL_ARCHIVE "ultralight-sdk-win-x64.7z")
    set(UL_HASH "")  # populate after first successful download
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64")
        set(UL_ARCHIVE "ultralight-sdk-mac-arm64.tar.gz")
    else()
        set(UL_ARCHIVE "ultralight-sdk-mac-x64.tar.gz")
    endif()
    set(UL_HASH "")
else()
    set(UL_ARCHIVE "ultralight-sdk-linux-x64.tar.gz")
    set(UL_HASH "")
endif()

include(FetchContent)
FetchContent_Declare(ultralight_sdk
    URL "${UL_BASE_URL}/${UL_ARCHIVE}"
)
FetchContent_MakeAvailable(ultralight_sdk)

# Locate SDK root (FetchContent places it in ultralight_sdk_SOURCE_DIR)
set(UL_SDK_ROOT "${ultralight_sdk_SOURCE_DIR}")

# Platform-specific library paths and filenames
if(WIN32)
    set(UL_LIB_DIR  "${UL_SDK_ROOT}/lib")
    set(UL_BIN_DIR  "${UL_SDK_ROOT}/bin")
    set(UL_CORE_LIB "${UL_LIB_DIR}/UltralightCore.lib")
    set(UL_WEB_LIB  "${UL_LIB_DIR}/WebCore.lib")
    set(UL_UI_LIB   "${UL_LIB_DIR}/Ultralight.lib")
    set(UL_CORE_DLL "${UL_BIN_DIR}/UltralightCore.dll")
    set(UL_WEB_DLL  "${UL_BIN_DIR}/WebCore.dll")
    set(UL_UI_DLL   "${UL_BIN_DIR}/Ultralight.dll")
elseif(APPLE)
    set(UL_LIB_DIR  "${UL_SDK_ROOT}/bin")
    set(UL_CORE_LIB "${UL_LIB_DIR}/libUltralightCore.dylib")
    set(UL_WEB_LIB  "${UL_LIB_DIR}/libWebCore.dylib")
    set(UL_UI_LIB   "${UL_LIB_DIR}/libUltralight.dylib")
else()
    set(UL_LIB_DIR  "${UL_SDK_ROOT}/bin")
    set(UL_CORE_LIB "${UL_LIB_DIR}/libUltralightCore.so")
    set(UL_WEB_LIB  "${UL_LIB_DIR}/libWebCore.so")
    set(UL_UI_LIB   "${UL_LIB_DIR}/libUltralight.so")
endif()

set(UL_INCLUDE_DIR "${UL_SDK_ROOT}/include")
set(UL_RESOURCES_DIR "${UL_SDK_ROOT}/bin/resources"
    CACHE PATH "Ultralight runtime resources directory")

# Helper to create a SHARED IMPORTED target
function(_ul_add_target target_name lib_path dll_path)
    add_library(${target_name} SHARED IMPORTED GLOBAL)
    if(WIN32)
        set_target_properties(${target_name} PROPERTIES
            IMPORTED_IMPLIB   "${lib_path}"
            IMPORTED_LOCATION "${dll_path}"
        )
    else()
        set_target_properties(${target_name} PROPERTIES
            IMPORTED_LOCATION "${lib_path}"
        )
    endif()
    set_target_properties(${target_name} PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${UL_INCLUDE_DIR}"
    )
endfunction()

if(WIN32)
    _ul_add_target(Ultralight::Core "${UL_CORE_LIB}" "${UL_CORE_DLL}")
    _ul_add_target(Ultralight::Web  "${UL_WEB_LIB}"  "${UL_WEB_DLL}")
    _ul_add_target(Ultralight::UI   "${UL_UI_LIB}"   "${UL_UI_DLL}")
else()
    _ul_add_target(Ultralight::Core "${UL_CORE_LIB}" "")
    _ul_add_target(Ultralight::Web  "${UL_WEB_LIB}"  "")
    _ul_add_target(Ultralight::UI   "${UL_UI_LIB}"   "")
endif()

# Propagate include dirs transitively from UI (the main target apps link to)
target_link_libraries(Ultralight::UI INTERFACE Ultralight::Core Ultralight::Web)

message(STATUS "Ultralight SDK: ${UL_SDK_ROOT}")
message(STATUS "Ultralight resources: ${UL_RESOURCES_DIR}")
