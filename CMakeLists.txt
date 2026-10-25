cmake_minimum_required(VERSION 3.21)

project(maa-capture
    VERSION 1.0.0
    DESCRIPTION "Standalone cross-platform screencap library extracted from MaaFramework"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MWS_ENABLE_FRAMEPOOL "Enable FramePool (Windows.Graphics.Capture) screencap" ON)
option(MWS_BUILD_SAMPLES "Build the sample executables" ON)
option(MWS_WIN32_COMPATIBLE "Use legacy DPI scaling fallback (see HwndUtils)" OFF)
option(MWS_ENABLE_PIPEWIRE "Enable PipeWire screencap unit (Linux; needs libpipewire-0.3 + libdrm)" ON)
option(MWS_ENABLE_WLR "Enable wlr-screencopy screencap unit (Linux; needs wayland-client)" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# .mm（Objective-C++）与 C++ 代码共用头文件，保持同一语言标准
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# 源码为 UTF-8（含中文注释）：对 MSVC 加 /utf-8，GCC/Clang 加字符集选项，
# 避免在 GBK 代码页下误读多字节序列导致注释吞行/预处理错乱（C1020/C4819）。
# 注意 GCC/Clang 的执行字符集选项是 -fexec-charset（而非 -fexecution-charset）
if(MSVC)
    add_compile_options(/utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 统一 Release 链接优化：VS 生成器默认走 /INCREMENTAL（增量链接会禁用
# /OPT:REF 与 /OPT:ICF，导致未引用代码/重复 COMDAT 全部保留、产物明显膨胀）；
# 这里对 MSVC 各产物类型追加与 Ninja 一致的裁剪/合并标志，Debug 不受影响
if(MSVC)
    foreach(_linker SHARED EXE MODULE)
        string(APPEND CMAKE_${_linker}_LINKER_FLAGS_RELEASE " /INCREMENTAL:NO /OPT:REF /OPT:ICF")
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
# 源文件布局：source/ 顶层平铺的是 Win32 单元 + 跨平台公共实现（下方显式列出），
# source/Linux、source/MacOS 为对应平台的单元与基础设施
#（协议生成代码随仓库提供，无需 wayland-scanner）
set(MWS_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/CustomScreencap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/ScreencapMgr.cpp)

file(GLOB_RECURSE MWS_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp)

if(WIN32)
    # source/*.cpp 同时覆盖公共实现与 Win32 单元
    file(GLOB MWS_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
elseif(APPLE)
    enable_language(OBJCXX)
    file(GLOB MWS_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/source/MacOS/*.mm)
    list(APPEND MWS_SOURCES ${MWS_COMMON_SOURCES})
else()
    file(GLOB MWS_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/source/Linux/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/source/Linux/Wayland/*.cpp)
    # wlr-screencopy 协议生成代码位于公共头目录（WaylandHelper.h 按相对路径
    # 引用其中的 .h），其中 -priv.c 需编入目标以提供接口符号
    file(GLOB MWS_PROTOCOL_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/maa-capture/Linux/Protocol/*.c)
    list(APPEND MWS_SOURCES ${MWS_PROTOCOL_SOURCES} ${MWS_COMMON_SOURCES})

    # Linux 截图依赖在源列表确定前检测：缺失时把对应单元裁出编译（自动降级，
    # 库仍可构建并使用外部自定义单元），MWS_*_DETECTED 供下方链接段使用
    find_package(PkgConfig QUIET)
    set(MWS_WLR_DETECTED FALSE)
    set(MWS_PIPEWIRE_DETECTED FALSE)
    if(MWS_ENABLE_WLR AND PkgConfig_FOUND)
        pkg_check_modules(MWS_WAYLAND QUIET wayland-client)
        if(MWS_WAYLAND_FOUND)
            set(MWS_WLR_DETECTED TRUE)
        endif()
    endif()
    if(MWS_ENABLE_PIPEWIRE AND PkgConfig_FOUND)
        pkg_check_modules(MWS_PIPEWIRE QUIET libpipewire-0.3)
        pkg_check_modules(MWS_DRM QUIET libdrm)
        if(MWS_PIPEWIRE_FOUND AND MWS_DRM_FOUND)
            set(MWS_PIPEWIRE_DETECTED TRUE)
        endif()
    endif()
    if(NOT MWS_WLR_DETECTED)
        list(REMOVE_ITEM MWS_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/source/Linux/WlrScreencap.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/source/Linux/MemfdBuffer.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/source/Linux/Wayland/WaylandClient.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/source/Linux/Wayland/WaylandHelper.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/maa-capture/Linux/Protocol/wlr-screencopy-priv.c)
    endif()
    if(NOT MWS_PIPEWIRE_DETECTED)
        list(REMOVE_ITEM MWS_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/source/Linux/PipeWireScreencap.cpp)
    endif()
endif()

add_library(maa-capture SHARED ${MWS_SOURCES} ${MWS_HEADERS})
add_library(maa-capture::maa-capture ALIAS maa-capture)

# C++ 公共 API（ScreencapMgr/ScreencapBase/内置单元）同样需要从 DLL 导出，
# 自动生成 .def 导出全部符号（C API 已用 MAA_WS_EXPORT 单独标记）
set_target_properties(maa-capture PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_include_directories(maa-capture
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source)

target_compile_definitions(maa-capture
    PRIVATE MWS_BUILDING_LIBRARY
    PUBLIC MWS_USE_LIBRARY)

if(MWS_WIN32_COMPATIBLE)
    target_compile_definitions(maa-capture PRIVATE MWS_WIN32_COMPATIBLE=1)
endif()

if(MWS_ENABLE_FRAMEPOOL)
    # Windows.Graphics.Capture 需要 Win10 SDK 10.0.22000+（由头文件自行检测兜底）
    target_compile_definitions(maa-capture PRIVATE MWS_FRAMEPOOL_AVAILABLE=1)
endif()

# 无第三方依赖：图像/颜色转换由 include/maa-capture/Image.hpp 内置
# （从 OpenCV 源码提取的最小实现），仅依赖各平台系统 API 与 C++ 标准库

if(WIN32)
    target_compile_definitions(maa-capture PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(maa-capture PRIVATE
        d3d11 dxgi dwmapi runtimeobject winmm)
elseif(APPLE)
    # ScreenCaptureKit 截图单元（macOS 14+ SDK）；显示器名称查询需 AppKit
    find_library(MWS_APPLICATION_SERVICES ApplicationServices REQUIRED)
    find_library(MWS_APP_KIT AppKit REQUIRED)
    find_library(MWS_CORE_FOUNDATION CoreFoundation REQUIRED)
    find_library(MWS_CORE_MEDIA CoreMedia REQUIRED)
    find_library(MWS_CORE_VIDEO CoreVideo REQUIRED)
    find_library(MWS_SCREEN_CAPTURE_KIT ScreenCaptureKit REQUIRED)
    target_compile_definitions(maa-capture PRIVATE MWS_SCK_AVAILABLE=1)
    target_link_libraries(maa-capture PRIVATE
        ${MWS_APPLICATION_SERVICES}
        ${MWS_APP_KIT}
        ${MWS_CORE_FOUNDATION}
        ${MWS_CORE_MEDIA}
        ${MWS_CORE_VIDEO}
        ${MWS_SCREEN_CAPTURE_KIT})
else()
    # Linux：wlr-screencopy（Wayland）与 PipeWire 截图单元的链接配置，
    # 源文件已按 MWS_*_DETECTED 在上方裁剪
    if(MWS_ENABLE_WLR AND MWS_WLR_DETECTED)
        message(STATUS "maa-capture: wlr-screencopy unit enabled")
        target_compile_definitions(maa-capture PRIVATE MWS_WLR_AVAILABLE=1)
        target_include_directories(maa-capture PRIVATE ${MWS_WAYLAND_INCLUDE_DIRS})
        target_link_libraries(maa-capture PRIVATE ${MWS_WAYLAND_LIBRARIES})
    elseif(MWS_ENABLE_WLR)
        message(WARNING "maa-capture: wayland-client not found, wlr-screencopy unit disabled")
    endif()

    if(MWS_ENABLE_PIPEWIRE AND MWS_PIPEWIRE_DETECTED)
        message(STATUS "maa-capture: PipeWire unit enabled")
        target_compile_definitions(maa-capture PRIVATE MWS_PIPEWIRE_AVAILABLE=1)
        target_include_directories(maa-capture PRIVATE
            ${MWS_PIPEWIRE_INCLUDE_DIRS} ${MWS_DRM_INCLUDE_DIRS})
        target_link_libraries(maa-capture PRIVATE ${MWS_PIPEWIRE_LIBRARIES})
    elseif(MWS_ENABLE_PIPEWIRE)
        message(WARNING "maa-capture: libpipewire-0.3/libdrm not found, PipeWire unit disabled")
    endif()

    # 无任何系统截图依赖可用时，库仍可构建（仅外部自定义单元可用）
endif()

# ---------------------------------------------------------------------------
# Samples
# ---------------------------------------------------------------------------
if(MWS_BUILD_SAMPLES)
    add_executable(mws_sample_cpp_interface
        ${CMAKE_CURRENT_SOURCE_DIR}/sample/external_cpp_interface.cpp)
    target_link_libraries(mws_sample_cpp_interface PRIVATE maa-capture::maa-capture)

    add_executable(mws_sample_c_callback
        ${CMAKE_CURRENT_SOURCE_DIR}/sample/external_c_callback.cpp)
    target_link_libraries(mws_sample_c_callback PRIVATE maa-capture::maa-capture)

    # 纯 C 消费示例：验证 C 回调接口可被 C 语言直接使用
    add_executable(mws_sample_c_only
        ${CMAKE_CURRENT_SOURCE_DIR}/sample/external_c_only.c)
    target_link_libraries(mws_sample_c_only PRIVATE maa-capture::maa-capture)
    target_compile_features(mws_sample_c_only PRIVATE c_std_11)

    # 纯 C 消费截图管理器 C API
    add_executable(mws_sample_c_manager
        ${CMAKE_CURRENT_SOURCE_DIR}/sample/external_c_manager.c)
    target_link_libraries(mws_sample_c_manager PRIVATE maa-capture::maa-capture)
    target_compile_features(mws_sample_c_manager PRIVATE c_std_11)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS maa-capture
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/maa-capture
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})