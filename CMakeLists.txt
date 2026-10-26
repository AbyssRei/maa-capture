cmake_minimum_required(VERSION 3.21)

project(maa-capture
    VERSION 1.0.0
    DESCRIPTION "Standalone cross-platform screen and window capture library"
    LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(MWS_ENABLE_FRAMEPOOL "Enable Windows.Graphics.Capture when the SDK supports it" ON)
option(MWS_BUILD_SAMPLES "Build the sample executables" ${PROJECT_IS_TOP_LEVEL})
option(MWS_BUILD_TESTS "Build native API regression tests" ${PROJECT_IS_TOP_LEVEL})
option(MWS_BUILD_DESKTOP_TESTS "Build opt-in Windows desktop color/source verification" OFF)
option(MWS_ENABLE_PIPEWIRE "Enable PipeWire capture on Linux when dependencies are available" ON)
option(MWS_ENABLE_WLR "Enable wlr-screencopy capture on Linux when dependencies are available" ON)

if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Keep common code separate from explicit platform source lists.
set(MWS_SOURCES source/CustomScreencap.cpp source/ScreencapMgr.cpp
    source/CaptureTypes.cpp source/MonitorSnapshot.cpp source/ScreencapBase.cpp)
set(MWS_FRAMEPOOL_ENABLED 0)
set(MWS_WLR_DETECTED 0)
set(MWS_PIPEWIRE_DETECTED 0)
set(MWS_SCK_DETECTED 0)

if(WIN32)
    list(APPEND MWS_SOURCES
        source/Windows/MonitorSnapshot.cpp
        source/Windows/DesktopDupScreencap.cpp
        source/Windows/DesktopDupWindowScreencap.cpp
        source/Windows/FramePoolScreencap.cpp
        source/Windows/GdiScreencap.cpp
        source/Windows/PrintWindowScreencap.cpp
        source/Windows/PseudoMinimizeHelper.cpp
        source/Windows/ScreenDCScreencap.cpp)
    if(MWS_ENABLE_FRAMEPOOL)
        include(CheckCXXSourceCompiles)
        function(mws_check_framepool_sdk)
            set(CMAKE_CXX_STANDARD 20)
            check_cxx_source_compiles("#include <sdkddkver.h>
#if !defined(WDK_NTDDI_VERSION) || WDK_NTDDI_VERSION < 0x0A00000B
#error Windows 10 SDK 10.0.22000 or newer is required
#endif
#include <winrt/Windows.Graphics.Capture.h>
int main() { return 0; }" MWS_FRAMEPOOL_SDK_AVAILABLE)
        endfunction()
        mws_check_framepool_sdk()
        if(MWS_FRAMEPOOL_SDK_AVAILABLE)
            set(MWS_FRAMEPOOL_ENABLED 1)
        else()
            message(STATUS "maa-capture: FramePool disabled because the Windows SDK is unsupported")
        endif()
    endif()
elseif(APPLE)
    enable_language(OBJCXX)
    list(APPEND MWS_SOURCES source/MacOS/ScreenCaptureKitScreencap.mm source/MacOS/MonitorSnapshot.mm)
    set(MWS_SCK_DETECTED 1)
else()
    find_package(PkgConfig QUIET)
    if(MWS_ENABLE_WLR AND PkgConfig_FOUND)
        pkg_check_modules(MWS_WAYLAND QUIET IMPORTED_TARGET wayland-client)
        if(MWS_WAYLAND_FOUND)
            set(MWS_WLR_DETECTED 1)
            list(APPEND MWS_SOURCES
                source/Linux/MemfdBuffer.cpp
                source/Linux/WlrScreencap.cpp
                source/Linux/Wayland/WaylandClient.cpp
                source/Linux/Wayland/WaylandHelper.cpp
                include/maa-capture/Linux/Protocol/wlr-screencopy-priv.c)
        endif()
    endif()
    if(MWS_ENABLE_PIPEWIRE AND PkgConfig_FOUND)
        pkg_check_modules(MWS_PIPEWIRE QUIET IMPORTED_TARGET libpipewire-0.3)
        pkg_check_modules(MWS_DRM QUIET IMPORTED_TARGET libdrm)
        if(MWS_PIPEWIRE_FOUND AND MWS_DRM_FOUND)
            set(MWS_PIPEWIRE_DETECTED 1)
            list(APPEND MWS_SOURCES source/Linux/PipeWireScreencap.cpp)
        endif()
    endif()
    if(MWS_ENABLE_WLR AND NOT MWS_WLR_DETECTED)
        message(STATUS "maa-capture: wayland-client unavailable; Wlr disabled")
    endif()
    if(MWS_ENABLE_PIPEWIRE AND NOT MWS_PIPEWIRE_DETECTED)
        message(STATUS "maa-capture: PipeWire/libdrm unavailable; PipeWire disabled")
    endif()
endif()

add_library(maa-capture SHARED ${MWS_SOURCES})
add_library(maa-capture::maa-capture ALIAS maa-capture)
set_target_properties(maa-capture PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 2
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    CXX_EXTENSIONS OFF
    OBJCXX_STANDARD 20
    OBJCXX_STANDARD_REQUIRED ON
    OBJCXX_EXTENSIONS OFF)
target_compile_features(maa-capture PUBLIC cxx_std_20)
target_include_directories(maa-capture
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(maa-capture
    PRIVATE MWS_BUILDING_LIBRARY
    PUBLIC MWS_USE_LIBRARY
        MWS_FRAMEPOOL_AVAILABLE=${MWS_FRAMEPOOL_ENABLED}
        MWS_WLR_AVAILABLE=${MWS_WLR_DETECTED}
        MWS_PIPEWIRE_AVAILABLE=${MWS_PIPEWIRE_DETECTED}
        MWS_SCK_AVAILABLE=${MWS_SCK_DETECTED})
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE MWS_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT MWS_COMMIT)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/SOURCE_COMMIT")
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/SOURCE_COMMIT" MWS_COMMIT LIMIT_COUNT 1)
    else()
        set(MWS_COMMIT "unknown")
    endif()
else()
    execute_process(COMMAND git status --porcelain --untracked-files=normal
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE MWS_DIRTY ERROR_QUIET)
    if(MWS_DIRTY)
        string(APPEND MWS_COMMIT "-dirty")
    endif()
endif()
target_compile_definitions(maa-capture PRIVATE MWS_VERSION="${PROJECT_VERSION}" MWS_COMMIT="${MWS_COMMIT}")
set(MWS_SDK "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
if(APPLE)
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-version OUTPUT_VARIABLE MWS_SDK OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
elseif(NOT WIN32)
    set(MWS_SDK "system headers (${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION})")
endif()
configure_file(cmake/build-manifest.json.in ${CMAKE_CURRENT_BINARY_DIR}/build-manifest.json.in @ONLY)
add_custom_command(TARGET maa-capture POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DNATIVE_FILE=$<TARGET_FILE:maa-capture>"
        "-DTEMPLATE=${CMAKE_CURRENT_BINARY_DIR}/build-manifest.json.in"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/maa-capture-build-$<CONFIG>.json"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/write-provenance.cmake)
add_custom_target(maa-capture-provenance ALL
    COMMAND ${CMAKE_COMMAND}
        "-DNATIVE_FILE=$<TARGET_FILE:maa-capture>"
        "-DTEMPLATE=${CMAKE_CURRENT_BINARY_DIR}/build-manifest.json.in"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/maa-capture-build-$<CONFIG>.json"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/write-provenance.cmake
    DEPENDS maa-capture)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/maa-capture-build-$<CONFIG>.json"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/maa-capture RENAME build-manifest.json)
# Options belong to this target so add_subdirectory does not alter parent targets.
if(MSVC)
    target_compile_options(maa-capture PRIVATE /utf-8)
    target_link_options(maa-capture PRIVATE
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)
else()
    target_compile_options(maa-capture PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX,OBJCXX>:-finput-charset=UTF-8>"
        "$<$<COMPILE_LANGUAGE:C,CXX,OBJCXX>:-fexec-charset=UTF-8>")
endif()

if(WIN32)
    target_compile_definitions(maa-capture PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(maa-capture PRIVATE d3d11 dxgi dwmapi runtimeobject winmm shcore)
elseif(APPLE)
    foreach(framework ApplicationServices AppKit CoreFoundation CoreMedia CoreVideo ScreenCaptureKit)
        find_library(MWS_${framework}_FRAMEWORK ${framework} REQUIRED)
        target_link_libraries(maa-capture PRIVATE ${MWS_${framework}_FRAMEWORK})
    endforeach()
else()
    if(MWS_WLR_DETECTED)
        target_link_libraries(maa-capture PUBLIC PkgConfig::MWS_WAYLAND)
    endif()
    if(MWS_PIPEWIRE_DETECTED)
        target_link_libraries(maa-capture PUBLIC PkgConfig::MWS_PIPEWIRE PRIVATE PkgConfig::MWS_DRM)
    endif()
endif()

if(MWS_BUILD_SAMPLES)
    foreach(sample cpp_interface c_callback c_only c_manager)
        if(sample MATCHES "^c_(only|manager)$")
            set(extension c)
        else()
            set(extension cpp)
        endif()
        add_executable(mws_sample_${sample} sample/external_${sample}.${extension})
        target_link_libraries(mws_sample_${sample} PRIVATE maa-capture::maa-capture)
        target_compile_features(mws_sample_${sample} PRIVATE c_std_11)
        if(MSVC)
            target_compile_options(mws_sample_${sample} PRIVATE /utf-8)
        endif()
    endforeach()
endif()

if(MWS_BUILD_TESTS)
    enable_testing()
    add_executable(maa_capture_native_smoke tests/native_smoke.cpp)
    target_link_libraries(maa_capture_native_smoke PRIVATE maa-capture::maa-capture)
    add_test(NAME native_api COMMAND maa_capture_native_smoke)
    add_executable(maa_capture_contracts tests/capture_contracts.cpp source/MonitorSnapshot.cpp)
    target_include_directories(maa_capture_contracts PRIVATE source)
    target_link_libraries(maa_capture_contracts PRIVATE maa-capture::maa-capture)
    add_test(NAME capture_contracts COMMAND maa_capture_contracts)
endif()
if(WIN32 AND MWS_BUILD_DESKTOP_TESTS)
    add_executable(maa_capture_desktop tests/windows_desktop.cpp)
    target_link_libraries(maa_capture_desktop PRIVATE maa-capture::maa-capture)
    if(MSVC)
        target_compile_options(maa_capture_desktop PRIVATE /utf-8)
    endif()
endif()

install(TARGETS maa-capture EXPORT maa-capture-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/maa-capture DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(MWS_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/maa-capture)
configure_package_config_file(cmake/maa-capture-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/maa-capture-config.cmake
    INSTALL_DESTINATION ${MWS_INSTALL_CMAKEDIR})
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/maa-capture-config-version.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT maa-capture-targets NAMESPACE maa-capture::
    DESTINATION ${MWS_INSTALL_CMAKEDIR})
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/maa-capture-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/maa-capture-config-version.cmake
    DESTINATION ${MWS_INSTALL_CMAKEDIR})
