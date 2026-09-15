cmake_minimum_required(VERSION 3.21)
project(Phoenix VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# MSYS2 MinGW Qt location (override with -DCMAKE_PREFIX_PATH=... if needed)
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/msys64/mingw64")
endif()

# Release packaging builds a windowed (WIN32) executable with no console.
# Development keeps the console so --self-test / --self-test-* output stays
# visible.
option(PHOENIX_RELEASE "Build without a console window (for the installer)" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# Embed the Phoenix icon into the .exe itself (window icon comes from Qt).
configure_file(src/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/app.rc PROPERTIES LANGUAGE RC)

if(PHOENIX_RELEASE)
    add_executable(Phoenix WIN32
        src/main.cpp
        src/core/HttpClient.cpp
        src/core/ResumeStore.cpp
        src/core/DownloadEngine.cpp
        src/core/DownloadQueue.cpp
        src/core/PowerControl.cpp
        src/core/RateLimiter.cpp
        src/core/UrlCodec.cpp
        src/core/UrlMatcher.cpp
        src/core/ProtocolRegistrar.cpp
        src/core/SingleInstance.cpp
        src/core/HttpListener.cpp
        src/core/ClipboardWatcher.cpp
        src/core/SettingsStore.cpp
        src/core/SessionStore.cpp
        src/core/NativeHost.cpp
        src/gui/MainWindow.cpp
        src/gui/AddDialog.cpp
        src/gui/SettingsDialog.cpp
        src/gui/I18n.cpp
        Phoenix.qrc
        ${CMAKE_CURRENT_BINARY_DIR}/app.rc
    )
else()
    add_executable(Phoenix
        src/main.cpp
        src/core/HttpClient.cpp
        src/core/ResumeStore.cpp
        src/core/DownloadEngine.cpp
        src/core/DownloadQueue.cpp
        src/core/PowerControl.cpp
        src/core/RateLimiter.cpp
        src/core/UrlCodec.cpp
        src/core/UrlMatcher.cpp
        src/core/ProtocolRegistrar.cpp
        src/core/SingleInstance.cpp
        src/core/HttpListener.cpp
        src/core/ClipboardWatcher.cpp
        src/core/SettingsStore.cpp
        src/core/SessionStore.cpp
        src/core/NativeHost.cpp
        src/gui/MainWindow.cpp
        src/gui/AddDialog.cpp
        src/gui/SettingsDialog.cpp
        src/gui/I18n.cpp
        Phoenix.qrc
        ${CMAKE_CURRENT_BINARY_DIR}/app.rc
    )
endif()

target_include_directories(Phoenix PRIVATE src)
target_link_libraries(Phoenix PRIVATE Qt6::Widgets Qt6::Network winhttp powrprof ws2_32)
