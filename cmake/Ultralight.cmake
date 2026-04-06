# cmake/Ultralight.cmake
# Downloads the Ultralight v1.4.0-beta prebuilt SDK (commit 208d653) for the
# current platform from the public Ultralight CDN, creates imported
# shared-library targets, and schedules post-build copy steps.
#
# Targets created:
#   Ultralight::Core     — UltralightCore
#   Ultralight::Web      — WebCore
#   Ultralight::UI       — Ultralight
#   Ultralight::AppCore  — AppCore (provides GetPlatformFontLoader, GetPlatformFileSystem)

set(UL_CDN_BASE "https://ultralight-sdk.sfo2.cdn.digitaloceanspaces.com")
set(UL_SHA "208d653")  # v1.4.0-beta

# Detect platform + architecture
if(WIN32)
    set(UL_ARCHIVE "ultralight-sdk-${UL_SHA}-win-x64.7z")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64")
        set(UL_ARCHIVE "ultralight-sdk-${UL_SHA}-mac-arm64.7z")
    else()
        set(UL_ARCHIVE "ultralight-sdk-${UL_SHA}-mac-x64.7z")
    endif()
else()
    set(UL_ARCHIVE "ultralight-sdk-${UL_SHA}-linux-x64.7z")
endif()

include(FetchContent)
FetchContent_Declare(ultralight_sdk
    URL "${UL_CDN_BASE}/${UL_ARCHIVE}"
)
FetchContent_MakeAvailable(ultralight_sdk)

# Locate SDK root — FetchContent sets ultralight_sdk_SOURCE_DIR.
# The archive is flat (no wrapping subdirectory), so SOURCE_DIR is the SDK root.
set(UL_SDK_ROOT "${ultralight_sdk_SOURCE_DIR}")

# Platform-specific library paths and filenames
if(WIN32)
    set(UL_LIB_DIR      "${UL_SDK_ROOT}/lib")
    set(UL_BIN_DIR      "${UL_SDK_ROOT}/bin")
    set(UL_CORE_LIB     "${UL_LIB_DIR}/UltralightCore.lib")
    set(UL_WEB_LIB      "${UL_LIB_DIR}/WebCore.lib")
    set(UL_UI_LIB       "${UL_LIB_DIR}/Ultralight.lib")
    set(UL_APPCORE_LIB  "${UL_LIB_DIR}/AppCore.lib")
    set(UL_CORE_DLL     "${UL_BIN_DIR}/UltralightCore.dll")
    set(UL_WEB_DLL      "${UL_BIN_DIR}/WebCore.dll")
    set(UL_UI_DLL       "${UL_BIN_DIR}/Ultralight.dll")
    set(UL_APPCORE_DLL  "${UL_BIN_DIR}/AppCore.dll")
elseif(APPLE)
    set(UL_LIB_DIR      "${UL_SDK_ROOT}/bin")
    set(UL_CORE_LIB     "${UL_LIB_DIR}/libUltralightCore.dylib")
    set(UL_WEB_LIB      "${UL_LIB_DIR}/libWebCore.dylib")
    set(UL_UI_LIB       "${UL_LIB_DIR}/libUltralight.dylib")
    set(UL_APPCORE_LIB  "${UL_LIB_DIR}/libAppCore.dylib")
else()
    set(UL_LIB_DIR      "${UL_SDK_ROOT}/bin")
    set(UL_CORE_LIB     "${UL_LIB_DIR}/libUltralightCore.so")
    set(UL_WEB_LIB      "${UL_LIB_DIR}/libWebCore.so")
    set(UL_UI_LIB       "${UL_LIB_DIR}/libUltralight.so")
    set(UL_APPCORE_LIB  "${UL_LIB_DIR}/libAppCore.so")
endif()

set(UL_INCLUDE_DIR "${UL_SDK_ROOT}/include")
# Note: resources/ is at the SDK root, not inside bin/
set(UL_RESOURCES_DIR "${UL_SDK_ROOT}/resources"
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
    _ul_add_target(Ultralight::Core    "${UL_CORE_LIB}"    "${UL_CORE_DLL}")
    _ul_add_target(Ultralight::Web     "${UL_WEB_LIB}"     "${UL_WEB_DLL}")
    _ul_add_target(Ultralight::UI      "${UL_UI_LIB}"      "${UL_UI_DLL}")
    _ul_add_target(Ultralight::AppCore "${UL_APPCORE_LIB}" "${UL_APPCORE_DLL}")
else()
    _ul_add_target(Ultralight::Core    "${UL_CORE_LIB}"   "")
    _ul_add_target(Ultralight::Web     "${UL_WEB_LIB}"    "")
    _ul_add_target(Ultralight::UI      "${UL_UI_LIB}"     "")
    _ul_add_target(Ultralight::AppCore "${UL_APPCORE_LIB}" "")
endif()

# Propagate include dirs transitively
target_link_libraries(Ultralight::UI      INTERFACE Ultralight::Core Ultralight::Web)
target_link_libraries(Ultralight::AppCore INTERFACE Ultralight::UI)

message(STATUS "Ultralight SDK: ${UL_SDK_ROOT}")
message(STATUS "Ultralight include: ${UL_INCLUDE_DIR}")
message(STATUS "Ultralight resources: ${UL_RESOURCES_DIR}")
