cmake_minimum_required(VERSION 3.16)

project(usb-switch
    VERSION 1.5.0
    DESCRIPTION "USB-Switch - USB safe removal tray tool"
    LANGUAGES CXX RC)

# ---------------------------------------------------------------------------
# Language / build defaults
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT MSVC)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(QUICKEJECT_RC_VERSION
    "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")

# ---------------------------------------------------------------------------
# Product identity: read once from AppInfo.h.in so the Windows version resource
# and the C++ sources cannot drift apart.
# ---------------------------------------------------------------------------
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/src/app/AppInfo.h.in QUICKEJECT_APP_INFO_TEMPLATE)

string(REGEX MATCH "#define QUICKEJECT_APP_NAME[ \t]+\"([^\"]*)\"" _unused
       "${QUICKEJECT_APP_INFO_TEMPLATE}")
set(QUICKEJECT_APP_NAME "${CMAKE_MATCH_1}")

string(REGEX MATCH "#define QUICKEJECT_APP_DESCRIPTION[ \t]+\"([^\"]*)\"" _unused
       "${QUICKEJECT_APP_INFO_TEMPLATE}")
set(QUICKEJECT_APP_DESCRIPTION "${CMAKE_MATCH_1}")

string(REGEX MATCH "#define QUICKEJECT_COMPANY[ \t]+\"([^\"]*)\"" _unused
       "${QUICKEJECT_APP_INFO_TEMPLATE}")
set(QUICKEJECT_COMPANY "${CMAKE_MATCH_1}")

string(REGEX MATCH "#define QUICKEJECT_COPYRIGHT[ \t]+\"([^\"]*)\"" _unused
       "${QUICKEJECT_APP_INFO_TEMPLATE}")
set(QUICKEJECT_COPYRIGHT "${CMAKE_MATCH_1}")

set(QUICKEJECT_VERSION "${PROJECT_VERSION}")
set(QUICKEJECT_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/usb-switch.ico")
set(QUICKEJECT_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")

# ---------------------------------------------------------------------------
# Generated files: version header + resource script (needs absolute paths)
# ---------------------------------------------------------------------------

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/AppInfo.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app/AppInfo.h
    @ONLY)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc
    @ONLY)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(QUICKEJECT_SOURCES
    src/main.cpp

    src/app/App.cpp
    src/app/SingleInstance.cpp
    src/app/Settings.cpp
    src/app/Logger.cpp
    src/app/AutoStart.cpp
    src/app/Updater.cpp

    src/core/DeviceManager.cpp
    src/core/MediaEject.cpp
    src/core/OpenFiles.cpp
    src/core/UsbPort.cpp
    src/core/DeviceNotifier.cpp
    src/core/VetoEvents.cpp
    src/core/Ejector.cpp

    src/ui/TrayIcon.cpp
    src/ui/MainWindow.cpp
    src/ui/Dialogs.cpp

    src/i18n/I18n.cpp
    src/i18n/Strings.cpp

    src/util/Json.cpp
    src/util/Str.cpp
)

add_executable(usb-switch WIN32 ${QUICKEJECT_SOURCES})

# The resource scripts live in their own target on purpose. They need none of
# the include directories the C++ sources use (quoted includes such as
# "resource.h" resolve relative to the .rc file itself), and windres cannot
# parse an -I path that contains spaces: it splits the argument and hands the
# remainder to cc1.exe as an input file, which fails the whole build when the
# project sits in a directory like "E:\Downloads\USB Switch". Keeping them out
# of the main target's include directories sidesteps that entirely.
add_library(usb-switch_resources OBJECT
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc
    resources/dialogs.rc)

target_link_libraries(usb-switch PRIVATE usb-switch_resources)

target_include_directories(usb-switch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_compile_definitions(usb-switch PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    _CRT_SECURE_NO_WARNINGS)

# setupapi      - device enumeration (GUID_DEVINTERFACE_DISK, DEVINST, ...)
# cfgmgr32      - CM_Request_Device_Eject / PnP veto reporting
# comctl32      - LoadIconWithScale, visual styles
# shell32       - Shell_NotifyIcon (tray)
# ole32         - CoTaskMemFree for shell folder helpers
# advapi32      - registry (settings backup path, autostart)
target_link_libraries(usb-switch PRIVATE
    setupapi
    cfgmgr32
    wevtapi
    comctl32
    shell32
    ole32
    advapi32
    user32
    gdi32)

if(MSVC)
    target_compile_options(usb-switch PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET usb-switch PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # MSVC's linker embeds a manifest itself. Supply ours to the linker so it
    # merges the manifests instead of emitting a second resource with id 1.
    target_compile_definitions(usb-switch_resources PRIVATE QE_MSVC_MANIFEST)
    target_link_options(usb-switch PRIVATE "/MANIFESTINPUT:${QUICKEJECT_MANIFEST_PATH}")
else()
    target_compile_options(usb-switch PRIVATE -Wall -Wextra)
    # wWinMain + no console window for a subsystem:windows executable.
    # -static keeps the binary self-contained: no libgcc/libstdc++/winpthread
    # DLLs have to ship next to usb-switch.exe.
    target_link_options(usb-switch PRIVATE -municode -mwindows -static)
endif()

# ---------------------------------------------------------------------------
# Install layout (used by `cmake --install`; the Inno Setup script mirrors it)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS usb-switch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
